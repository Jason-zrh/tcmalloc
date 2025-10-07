# Tcmalloc
## 项目简介
  该项⽬是实现⼀个⾼并发的内存池，他的原型是google的⼀个开源项⽬tcmalloc，tcmalloc全称
Thread-Caching Malloc，即线程缓存的malloc，实现了⾼效的多线程内存管理，⽤于替代系统的内
存分配相关的函数（malloc、free）。
  这个项⽬是把tcmalloc最核⼼的框架简化后拿出来，模拟实现出⼀个⾃⼰的⾼并发内存池，⽬的
就是学习tcamlloc的精华。

## 三大板块
- Thread Cache(线程缓存)
- Central Cache(中心缓存)
- Page Cache(页缓存)

## Thread Cache(线程缓存)

  线程缓存最大是256kb
  如果一个字节对齐一次的话将会有20w+自由链表，所以我们采用下面的对齐方式
  这种对齐方式可以做到10%左右的内碎片浪费
  | 对齐方式 | 申请内存块大小范围 | 自由链表的下标 |
  |---------|-------------------|--------------|
  | 8bits | [1, 128] | [0, 16) |
  | 16bits | [128 + 1, 1024] | [16, 72) |
  | 128bits | [1024 + 1, 8 * 1024] | [72, 128) |
  | 1024bits | [8 * 1024 + 1, 64 * 1024] | [128, 184) |
  | 8 * 1024bits | [64 * 1024 + 1, 256 * 1024] | [184, 208) |

  想要申请缓存需要做两件事，一个是内存对齐(如一个对象的大小是9byte， 经过对齐后应该是16byte)，另一个是计算桶的下标(仍然拿9byte举例子， 对齐后的是16byte，所以应该放在第二个桶中，对应的下标则为1， 如果一个对象是17byte则对齐到24byte, 放在第三个桶中，下标为2)。
  下面展示内存对齐和下标计算的核心代码
  ``` cpp
    // 内存对齐
    static size_t _RoundUp(size_t size, size_t alignNum)
    {
        return (size + alignNum - 1) & ~(alignNum - 1);
    }

    // 下标计算
    static inline size_t _Index(size_t byte, size_t alignNum_shift)
    {
        return ((byte + (1 << alignNum_shift) - 1) >> alignNum_shift) - 1;
    }
```
  每次线程需要内存的时候，应该先去_freeLists[index]位置找看看有没有被释放的内存块，如果没有的话则应该去向下一层Central Cache申请

  Thread Cache主逻辑
  ``` cpp
  // ThreadCache申请size大小的内存
  void* ThreadCache::Allocate(size_t size)
  {
      assert(size <= MAX_BYTES);
      // 先对齐内存, 计算应该给size什么对齐方式
      size_t alginSize = SizeClass::RoundUp(size);
      // 计算自由链表中桶的位置
      size_t index = SizeClass::Index(alginSize);
  
      // 自由链表中不为空，直接取一个内存对象返回
      if (!_freeLists[index].Empty())
      {
          return _freeLists[index].Pop();
      }
      // 自由链表为空，从CentralCaChe申请内存对象
      else
      {
          return FetchFromCentralCache(index, alginSize);
      }
  }
```

## Central Cache(中心缓存)

  Central Cache的构造与Thread Cache相似，都是由哈希桶构成的，不同的地方是Central Cache的每个桶中装的是一个SpanList，一个Span下挂着一串已经切成对应大小的内存块，且SpanList是以一个带头双向循环链表的形式维护。
  
Span节点的定义
``` cpp
// 管理大块跨度的内存空间
struct Span
{
    PAGE_ID _pageId = 0; // 多个大块内存的起始页的页号
    size_t _n = 0;       // 页的数量

    Span* _next = nullptr;     // 双向链表的结构
    Span* _prev = nullptr;

    size_t _useCount = 0; // 切好的小块内存被分给threadCache的计数

    void* _freeList = nullptr; // 自由链表
};
```
如Thread Cache主函数定义，当_freeList[index]是一个空桶的时候，要向Central Cache申请内存空间，但是这里有一个细节，Thread Cache有TLS(Thread Local Storage)可以不用加桶锁，但是当与Central Cache交互时，可能会有多个线程，为了线程安全，这里必须要给Central Cache加桶锁，所以当Thread Cache向Central Cache要内存块时，可以批量给出一些，这样就可以减少锁竞争。那么怎么评估每一批给出多少内存对象呢？这里使用慢增长反馈调节算法-->对象越大，给的越少；对象越小，给的越大。

``` cpp
// 慢开始算法，计算Thread Cache一次从Central Cache获得多少个对象, 使用此算法可以控制一次获得[2, 512]个对象
static size_t NumMovSize(size_t size)
{
    if (size == 0)
        return 0;
    // 如果一个对象较大，则最少分配2个
    size_t num = MAX_BYTES / size;
    if (num < 2)
        num = 2;
    // 如果一个对象较小，则最多分配512个
    if (num > 512)
        num = 512;
    return num;
}
```

同时由于Central Cache可以被所有线程访问到，所以应该设置为单例模式，我在这里采用了饿汉模式(将构造函数和拷贝构造设置为私有)，唯一对象类内声明，在实现的开始就定义。
``` cpp
// 单例模式
class CentralCache
{
public:
	// 获取一个非空Span
	Span* GetOneSpan(SpanList& spanList, size_t size);
	size_t FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size);
private:
	// 饿汉型单例模式
	// 构造函数和拷贝构造变成私有
	CentralCache()
	{ }
	CentralCache(const CentralCache&) = delete;

	// 类里面声明
	static CentralCache _sInst;
public:
	static CentralCache* GetInstance()
	{
		return &_sInst;
	}
private:
	// centralcache的哈希桶
	SpanList _spanList[NFREE_LISTS];
};
```
在向Central Cache要内存空间的时候，与Thread Cache中也相似，先看_spanList[index]位置中有没有span，若有span且span中_freeList不为空则批量返回，若没有则需要向Page Cache申请空间了
``` cpp
// threadcache向centralcache批量申请对象，返回值是成功申请对象的个数，前两个参数是输出型参数
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size)
{
	size_t index = SizeClass::Index(size);
	// 先上桶锁
	_spanList[index]._mtx.lock();
	Span* span = GetOneSpan(_spanList[index], size);
	// span取到了，且span里面不为空
	assert(span);
	assert(span->_freeList);
	// 从找的span中取出batchNum个变量
	start = span->_freeList;
	end = start;
	// 这里用end来遍历span
	size_t i = 0;
	size_t actualNum = 1;
	// 从span中获取batchNum个对象，如果不够则有几个拿几个
	while (i < batchNum - 1 && NextObj(end) != nullptr)
	{
		end = NextObj(end);
		i++;
		actualNum++;
	}
	span->_freeList = NextObj(end);
	NextObj(end) = nullptr;
	// 有锁就一定要解锁
	_spanList[index]._mtx.unlock();
	return actualNum;
}
```

## Page Cache(页缓存)

事实上，在页缓存中，我们使用的依然还是哈希桶的结构来存储一页内存，但是与Thread Cache和Central Cache不同的是，Page Cache的映射关系比上述两个更简单，Page Cahce用的是直接定址法，桶的编号是几，就代表有几页内存(假设一页内存是8kb = 8 * 1024b， 则对应的5号桶中就有一个5页的大块内存)，

接下来就应该完成NewSpan这个函数，思路相似，如果在Page Cache的SpanLists中找不到对应大小的页内存，我们应该去向下找更大的页，因为大页可以切分成我们需要的页和一个小页，当直到找到128kb没有的时候，就需要向系统申请大块内存了

``` cpp
// NewSpan代码逻辑







