#pragma once
#include "Common.h"

// 单例模式
class CentralCache
{
public:
	// 获取一个非空Span
	Span* GetOneSpan(SpanList& spanList, size_t size);
	size_t FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size);

	// 将⼀定数量的对象释放到span跨度
	void ReleaseListToSpans(void* start, size_t size);
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
	SpanList _spanLists[NFREE_LISTS];
};
