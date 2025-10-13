#pragma once
#include "Common.h"
#include "ThreadCache.h"
#include "PageCache.h"
#include "ObjectPool.h"

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
