#include "ThreadCache.h"

void* ThreadCache::Allocate(size_t size)
{
    assert(size <= MAX_BYTES);
    // 先对齐内存
    size_t alginSize = SizeClass::RoundUp(size);
    // 计算自由链表中桶的位置
    size_t index = SizeClass::Index(size);

    // 自由链表中不为空，直接取一个内存对象返回
    if(!_freeLists[index].Empty())
    {
        return _freeLists[index].Pop();
    }
    // 自由链表为空，从下一层中心缓存申请内存对象
    else
    {
        return FetchFromCentralCache(index, size);
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

// 从中心获取内存对象
void* ThreadCache::FetchFromCentralCache(size_t index, size_t size)
{
    // ...
    return nullptr;
}