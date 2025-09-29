#pragma once
#include "Common.h"
#include "ThreadCache.h"


// thead cache
static void* ConcurrentAlloc(size_t size)
{
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