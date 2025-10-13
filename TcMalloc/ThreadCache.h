#pragma once
#include "Common.h"

class ThreadCache
{
public:
    // 申请内存对象
    void* Allocate(size_t size);
    // 从中心获取内存对象
    void* FetchFromCentralCache(size_t index, size_t size);


    // 释放内存对象
    void ListTooLong(FreeList& list, size_t size);
    void Deallocate(void* ptr, size_t size);
private:
    // 哈希桶结构存储自由链表
    FreeList _freeLists[NFREE_LISTS];
};

// TLS (thread local storage )是一种变量储存方法，这个变量在它所在的线程内是全局可以访问的，但是不能被其他线程访问到
// 从而保持了数据的线程独立性, 实现了线程的TLS无锁访问
static thread_local ThreadCache* pTLSThreadCache = nullptr;  // 这里加一个static可以保持只在当前文件可见

