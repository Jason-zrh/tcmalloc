#pragma once
#include "Common.h"
#include "ObjectPool.h"

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
	PageCache()
	{ };
	PageCache(const PageCache&) = delete;
	static PageCache _sInst;

private:
	SpanList _spanLists[NPAGES];
	// 建立页号与span的映射关系，便于回收内存
	std::unordered_map<PAGE_ID, Span*> _idSpanMap;
	ObjectPool<Span> _spanPool;
};
