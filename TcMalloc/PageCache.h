#pragma once
#include "Common.h"


// PageCache也使用单例模式
class PageCache
{
public:
	static PageCache* GetInstance()
	{
		return &_sInst;
	}
	Span* NewSpan(size_t size);
	std::mutex _pageMtx;

private:
	PageCache()
	{ };
	PageCache(const PageCache&) = delete;
	static PageCache _sInst;

private:
	SpanList _spanLists[NPAGES];

};
