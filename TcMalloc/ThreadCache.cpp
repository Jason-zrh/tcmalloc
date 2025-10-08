#include "ThreadCache.h"
#include "CentralCache.h"


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

void ThreadCache::Deallocate(void* ptr, size_t size)
{
    assert(size <= MAX_BYTES);
    assert(ptr);
    // 计算桶的位置
    size_t index = SizeClass::Index(size);
    // 头插到自由链表
    _freeLists[index].Push(ptr);
}

// CentralCache获取内存对象
// CentralCache的结构也是哈希桶，不过每个桶中装的是含有切好内存对象的Span，这些span用双向链表的结构连在一起，映射关系与threadcache相同，所以可以直接把index传过来
void* ThreadCache::FetchFromCentralCache(size_t index, size_t size)
{
    // 这里使用慢开始反馈调节算法
    // 1.最开始不会一次要太多，因为可能要太多了可能用不完
    // 2.如果有不断这个内存大小的需求，就会不断增长，直到上限
    // 3.对象越小，上限越大，对象越大，上限越低

    // 这里ThreadCache虽然只要了一个，但是还是给出一批对象，这样可以减少锁竞争
    size_t batchNum = min(_freeLists[index].maxSize(), SizeClass::NumMovSize(size));
    if (batchNum == _freeLists[index].maxSize())
    {
        // 从1个对象开始获取，直到获取到最大值
        // 在这里可以调整反馈速度快慢
        _freeLists[index].maxSize() += 1;
    }


    // 输出型参数
    void* start = nullptr;
    void* end = nullptr;
    // 有多少内存就返回多少，如果centralcache里面的不够了，就返回给了多少个
    // 因为有可能是多个线程同时向centralcache发出需求，所以这里设为单例模式，并且每次要加桶锁
    size_t actulNum = CentralCache::GetInstance()->FetchRangeObj(start, end, batchNum, size);
    // 这里向Central Cache批量要，可能给不全，但是至少应该给一个，否则就要抛异常了
    assert(actulNum > 0);
    // 如果只开辟了一个，则直接返回
    if (actulNum == 1)
    {
        assert(start == end);
        return start;
    }
    // 返回值长度不止一个，则拿出一个来返回
    else
    {
        _freeLists[index].PushRange(NextObj(start), end);
        return start;
    }
}
