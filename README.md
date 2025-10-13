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

***

# 内存申请逻辑

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
  
  ### 内存对齐和下标计算的核心代码
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

### Thread Cache主逻辑
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
***

## Central Cache(中心缓存)

  Central Cache的构造与Thread Cache相似，都是由哈希桶构成的，不同的地方是Central Cache的每个桶中装的是一个SpanList，一个Span下挂着一串已经切成对应大小的内存块，且SpanList是以一个带头双向循环链表的形式维护。
  
### Span节点的定义
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

### 慢开始调节算法
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

### 单例模式定义
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

### FetchRangeObj函数实现
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

***

## Page Cache(页缓存)

事实上，在页缓存中，我们使用的依然还是哈希桶的结构来存储一页内存，但是与Thread Cache和Central Cache不同的是，Page Cache的映射关系比上述两个更简单，Page Cahce用的是直接定址法，桶的编号是几，就代表有几页内存(假设一页内存是8kb = 8 * 1024b， 则对应的5号桶中就有一个5页的大块内存)，

接下来就应该完成NewSpan这个函数，思路相似，如果在Page Cache的SpanLists中找不到对应大小的页内存，我们应该去向下找更大的页，因为大页可以切分成我们需要的页和一个小页，当直到找到128kb没有的时候，就需要向系统申请大块内存了

``` cpp
// NewSpan代码逻辑
Span* PageCache::NewSpan(size_t k)
{
	assert(k > 0);
	assert(k < NPAGES);
	// 先判断SpanList[k]位置是否为空
	// 不为空，从_spanLists给出一个
	if (!_spanLists[k].Empty())
	{
		return _spanLists[k].PopFront();
	}
	// 这个位置为空，去遍历后面的桶有没有有span的
	for (int i = k + 1; i < NPAGES; i++)
	{
		// 有一个span不为空
		if (!_spanLists[i].Empty())
		{
			// 先把它拿出来
			Span* nSpan = _spanLists[i].PopFront();
			// 进行切分，切成两个，一个k页的span和一个n - k页的span
			Span* kSpan = new Span;
			kSpan->_n = k;
			kSpan->_pageId = nSpan->_pageId;

			nSpan->_pageId += k;
			nSpan->_n -= k;
			// 切好了，需要挂到该在的位置
			_spanLists[nSpan->_n].PushFront(nSpan);
			return kSpan;
		}
	}
	// 走到这里是在128页也找不到span，这时候找堆要一个128页的Span
	Span* bigSpan = new Span;
	void* ptr = SystemAlloc(NPAGES - 1);
	bigSpan->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
	bigSpan->_n = NPAGES - 1;
	_spanLists[bigSpan->_n].PushFront(bigSpan);
	return NewSpan(k);
}

```

到此，内存申请逻辑完成。

***

# 内存释放逻辑

## ThreadCache(线程缓存)

当线程把内存对象还给threadcache的时候，每一个小内存对象会挂在对应的_freeList中，当内存对象积攒到一定数量后，会将这些内存对象返还给centralCache，在这里我们简化一下归还逻辑（当_freeList中挂的对象数与向centralCache一次批量申请的数量相等的时候就返回给centralCache) （真正的tcmalloc肯定比这个细节多的多，但这里只是简化，只学习其中核心部分）

### Thread Cache归还逻辑
``` cpp
void ThreadCache::Deallocate(void* ptr, size_t size)
{
    // 这里断言一下，防止发生错误
    assert(size <= MAX_BYTES);
    assert(ptr);
    // 计算归还桶的位置
    size_t index = SizeClass::Index(size);
    // 头插到自由链表
    _freeLists[index].Push(ptr);

    // 判断threadCache的_freeLists中已经回收的内存有没有超过批量一次申请的内存大小
    // 如果有则将他们批量回收
    if (_freeLists[index].Size() >= _freeLists[index].maxSize())
    {
        ListTooLong(_freeLists[index], size);
    }
}
```

## Central Cache(中心缓存)

当Cental Cache接受从threadCache返回来的内存块的时候，需要注意要把对应的内存块还到对应的Span中（在ThreadCache返回的内存对象不一定是来自一个Span，但是一定来自同一个_spanList），为了方便找到对应span，我们在Span的类中再加一个成员: unordered_map<PAGE_ID, Span*> _idSpanMap，这样在拿到threadcache返回的内存对象时只需要通过(PAGE_ID)ptr >> PAGE_SHIFT就可以拿到页号从而找到对应Span。

### Span的定义
``` cpp
struct Span
{
    PAGE_ID _pageId = 0; // 页号
    size_t _n = 0;       // 页的数量

    Span* _next = nullptr;     // 双向链表的结构
    Span* _prev = nullptr;

    size_t _useCount = 0; // 切好的小块内存被分给threadCache的计数
    size_t _objSize = 0;  // 切好的内存的大小

    // 记录这个span是否在使用
    bool _isUse = false;
    void* _freeList = nullptr; // 自由链表
};
```

在接受ThreadCache返回的内存对象后，Span->_useCount需要减1，当_useCount的值被减到1的时候，说明一个Span分出去的内存对象已经全部返还回来了，这时我们就可以将这个Span返还给PageCache，让PageCache进行前后合并后再重新挂起来，充分解决了外碎片问题。

### CentralCache归还逻辑
``` cpp
// 注意这里需要把每个小内存块归还到他们对应的span中，所以这里需要建立一个pageId与span的映射
void CentralCache::ReleaseListToSpans(void* start, size_t size)
{
	// 先计算下标
	size_t index = SizeClass::Index(size);
	// 上锁
	_spanLists[index]._mtx.lock();

	while (start)
	{
		// 记录下一个
		void* next = NextObj(start);
		// 找到对应span
		Span* span = PageCache::GetInstance()->MapObjectToSpan(start);
		// 头插
		NextObj(start) = span->_freeList;
		span->_freeList = start;

		// 这里用usecount来判断要不要还给pagecache
		span->_useCount--;
		if (span->_useCount == 0)
		{
			// 可以回收给pagecache了，pagecache再去尝试做前后页的合并
			_spanLists[index].Erase(span);
			span->_freeList = nullptr;
			span->_prev = nullptr;
			span->_next = nullptr;



			// 缓解内存碎片的问题
			_spanLists[index]._mtx.unlock();
			PageCache::GetInstance()->_pageMtx.lock();
			PageCache::GetInstance()->ReleaseSpanToPageCache(span);
			PageCache::GetInstance()->_pageMtx.unlock();
			_spanLists[index]._mtx.lock();

		}
		start = next;
	}
	// 解锁
	_spanLists[index]._mtx.unlock();
}
```

## PageCache(页缓存)

在CentralCache将Span返回的时候，PageCache需要计算Span前面的页号和后面的页号是否存在 / 是否正在使用 / 是否合起来超过NPAGES(最大页数)， 只要检测到对应的Span存在时，就会进行Span的合并直到不能进行合并，
在这里时候前面建立的_idSpanMap就可以再次派上用场。

### PageCache中页的合并逻辑
```cpp
void PageCache::ReleaseSpanToPageCache(Span* span)
{
	// 大于128页的span直接还给堆
	if (span->_n > NPAGES - 1)
	{
		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		SystemFree(ptr);
		//delete span;
		_spanPool.Delete(span);
		return;
	}
	else
	{
		// 先一直向前合并，一直合并到不能合并
		while (1)
		{
			PAGE_ID prevId = span->_pageId - 1;
			// 先向前找span，直到找不到，前面的页号就是span->_pageId - 1
			auto ret = _idSpanMap.find(prevId);
			if (ret == _idSpanMap.end())
			{
				break;
			}

			Span* prevSpan = ret->second;
			// 判断前一个span是否正在使用
			if (prevSpan->_isUse == true)
			{
				break;
			}
			// 判断两个合起来有没有超过最大页
			if (prevSpan->_n + span->_n >= NPAGES - 1)
			{
				break;
			}

			// 可以开始进行合并
			span->_pageId = prevSpan->_pageId;
			span->_n += prevSpan->_n;
			// 在_spanlist中删除掉，防止野指针
			_spanLists[prevSpan->_n].Erase(prevSpan);
			//delete prevSpan;
			_spanPool.Delete(prevSpan);
		}

		// 再向后合并
		while (1)
		{
			PAGE_ID nextId = span->_pageId + span->_n;
			auto ret = _idSpanMap.find(nextId);
			if (ret == _idSpanMap.end())
			{
				break;
			}
			Span* nextSpan = ret->second;
			if (nextSpan->_isUse == true)
			{
				break;
			}
			if (nextSpan->_n + span->_n >= NPAGES - 1)
			{
				break;
			}
			// 开始向后合并
			span->_n += nextSpan->_n;
			_spanLists[nextSpan->_n].Erase(nextSpan);
			//delete nextSpan;
			_spanPool.Delete(nextSpan);
		}


		// 向前和向后合并结束，把合并好的大块span插到应有的位置
		_spanLists[span->_n].PushFront(span);
		span->_isUse = false;
		// 这里还需要存一下映射关系
		_idSpanMap[span->_pageId] = span;
		_idSpanMap[span->_pageId + span->_n - 1] = span;
	}
}
```
当所有的小内存还回来的时候，则一定会合成一个大内存

到此，内存释放逻辑结束。

***

# 超过256KB的大块内存的申请和释放

在ThreadCache中，一次可以申请的最大内存对象为256KB，超过这个大小的将会直接向CentralCache要，这里需要分情况讨论的是，大块内存可以是大于256KB 但是小于最大一页的内存数量，这里是8K * 128，也可以是大于最大一页的数量。当内存大于最大一页的内存数量的时候，将会直接向堆申请

## 大块内存的申请

``` cpp
// 创建对象申请内存
static void* ConcurrentAlloc(size_t size)
{
    // 当申请内存大小大于threadcache的256kb时走大块内存申请流程
    if (size > MAX_BYTES)
    {
        size_t alignSize = SizeClass::RoundUp(size);
        size_t kpage = alignSize >> PAGE_SHIFT;

        PageCache::GetInstance()->_pageMtx.lock();

        Span* span = PageCache::GetInstance()->NewSpan(kpage);
        span->_objSize = size;

        PageCache::GetInstance()->_pageMtx.unlock();
        void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
        return ptr;
    }
    else
    {
        // 用TLS(thread local storage)来做到在threadcache申请内存的时候不需要加锁
        // 第一次申请的时候
        if (pTLSThreadCache == nullptr)
        {
            static ObjectPool<ThreadCache> tcPool;
            //pTLSThreadCache = new ThreadCache;
            pTLSThreadCache = tcPool.New();
        }

        // cout << std::this_thread::get_id() << ":" << pTLSThreadCache << std::endl;
        return pTLSThreadCache->Allocate(size);
    }
}
```

## 大块内存的释放

``` cpp
// 释放内存
static void ConcurrentFree(void* ptr)
{

    Span* span = PageCache::GetInstance()->MapObjectToSpan(ptr);
    size_t size = span->_objSize;

    if (size > MAX_BYTES)
    {
        PageCache::GetInstance()->_pageMtx.lock();
        PageCache::GetInstance()->ReleaseSpanToPageCache(span);
        PageCache::GetInstance()->_pageMtx.unlock();
    }
    else
    {
        assert(pTLSThreadCache);
        // 直接调用Deallocate函数
        pTLSThreadCache->Deallocate(ptr, size);
    }
}
```

***







