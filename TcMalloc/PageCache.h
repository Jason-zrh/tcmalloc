// #pragma once
// #include "Common.h"
// #include "ObjectPool.h"

// // PageCache也使用单例模式
// class PageCache
// {
// public:
// 	static PageCache* GetInstance()
// 	{
// 		return &_sInst;
// 	}
// 	Span* NewSpan(size_t size);
// 	Span* MapObjectToSpan(void* obj);
// 	void ReleaseSpanToPageCache(Span* span);

// 	std::mutex _pageMtx;
// private:
// 	PageCache()
// 	{ };
// 	PageCache(const PageCache&) = delete;
// 	static PageCache _sInst;

// private:
// 	SpanList _spanLists[NPAGES];
// 	// 建立页号与span的映射关系，便于回收内存
// 	std::unordered_map<PAGE_ID, Span*> _idSpanMap;
// 	ObjectPool<Span> _spanPool;
// };

// ============================================================== 使用基数树优化 ==============================================================

#pragma once
#include "Common.h"
#include "ObjectPool.h"
#include "PageMap.h"

// PageCache也使用单例模式
class PageCache
{
public:
	static PageCache* GetInstance()
	{
		return &_sInst;
	}
	Span* NewSpan(size_t size);
	Span* MapObjectToSpan(void* obj);
	void ReleaseSpanToPageCache(Span* span);

	std::mutex _pageMtx;
private:

	// 单例模式
	PageCache()
	{ };
	PageCache(const PageCache&) = delete;
	static PageCache _sInst;

private:
	SpanList _spanLists[NPAGES];
	// 建立页号与span的映射关系，便于回收内存
	// std::unordered_map<PAGE_ID, Span*> _idSpanMap;
	
	// 改用一层基数树进行页号与Span的映射
	TCMalloc_PageMap1<32 - PAGE_SHIFT> _idSpanMap;

	ObjectPool<Span> _spanPool;
};

