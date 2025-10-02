#pragma once
#include "Common.h"
#include "ThreadCache.h"


// 创建对象申请内存
static void* ConcurrentAlloc(size_t size)
{
    // 用TLS(thread local storage)来做到在threadcache申请内存的时候不需要加锁

    // 第一次申请的时候
    if (pTLSThreadCache == nullptr)
    {
        pTLSThreadCache = new ThreadCache;
    }

    cout << std::this_thread::get_id() << ":" << pTLSThreadCache << std::endl;
    return pTLSThreadCache->Allocate(size);
}



static void ConcurrentFree(void* ptr, size_t size)
{
    assert(pTLSThreadCache);
    pTLSThreadCache->Deallocate(ptr, size);
}