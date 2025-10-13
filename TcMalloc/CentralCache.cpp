#include "CentralCache.h"
#include "PageCache.h"

// 单例模式对象定义
CentralCache CentralCache::_sInst;

// threadcache向centralcache批量申请对象，返回值是成功申请对象的个数，前两个参数是输出型参数
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size)
{
	size_t index = SizeClass::Index(size);
	// 先上桶锁
	_spanLists[index]._mtx.lock();

	// 找centralcache的spanlist位置的是否有span
	Span* span = GetOneSpan(_spanLists[index], size);
	// span取到了，且span里面不为空
	assert(span); // 如果没取到会抛异常
	assert(span->_freeList); 
	// 从找的_freeList中取出batchNum个内存对象
	start = span->_freeList;
	end = start;
	// 这里用end来遍历_freeList
	size_t i = 0;
	size_t actualNum = 1;
	// 从span中获取batchNum个对象，如果不够则有几个拿几个
	while (i < batchNum - 1 && NextObj(end) != nullptr)
	{
		end = NextObj(end);
		i++;
		actualNum++;
	}
	// 这里分配给threadcache几个就把usecount加几
	span->_useCount += actualNum;
	// 这里把_freeList重新链接
	span->_freeList = NextObj(end);
	NextObj(end) = nullptr;
	// 有锁就一定要解锁
	_spanLists[index]._mtx.unlock();
	return actualNum;
}


// 传过来的参数是一个spanlist
Span* CentralCache::GetOneSpan(SpanList& spanlist, size_t size)
{
	// 两种情况：1.是spanlist中有span 2.没有了，pagecache中申请
	Span* it = spanlist.Begin();
	while (it != spanlist.End())
	{
		if (it->_freeList != nullptr)
		{
			// 在已有的spanList中找到了非空span
			return it;
		}
		else
		{
			it = it->_next;
		}
	}

	// 先把桶锁解开，这样可以方便其他线程可以继续进行回收内存对象的操作
	spanlist._mtx.unlock();
	// 在访问PageCache的时候需要给pagecache上一把大锁
	PageCache::GetInstance()->_pageMtx.lock();
	// 该找PageCache要了
	Span* span = PageCache::GetInstance()->NewSpan(SizeClass::NumMovePage(size));

	// 将分过来的span的使用情况改为true
	span->_isUse = true;
	span->_objSize = size;
	// 有上锁就有解锁
	PageCache::GetInstance()->_pageMtx.unlock();

	// 已经申请到span了，接下来要把span切好以后挂起来
	// 首先要通过页号算出页的起始地址,这里定义为char是为了方便++
	char* start = (char*)(span->_pageId << PAGE_SHIFT);
	// 计算一个span有多少内存
	size_t bytes = span->_n << PAGE_SHIFT;
	char* end = start + bytes;
	// 把大块内存切成自由链表挂起来
	// 先切一块做头，方便尾插
	span->_freeList = start;
	// 在这里可以切成每个对象大小的小块内存
	start += size;
	void* tail = span->_freeList;
	while (start < end)
	{
		// 这里推荐用尾插因为小块地址的内存是连续的，缓存命中比较高
		NextObj(tail) = start;
		// 更新尾
		tail = start;
		start += size;
	}
	NextObj(tail) = nullptr;
	spanlist._mtx.lock();
	spanlist.PushFront(span);
	return span;
}


// 注意这里需要把每个小内存块归还到他们对应的span中，所以这里需要建立一个pageId与span的映射
void CentralCache::ReleaseListToSpans(void* start, size_t size)
{
	// 先计算下标
	size_t index = SizeClass::Index(size);
	// 上锁
	_spanLists[index]._mtx.lock();

	while (start)
	{
		// 记录下一个
		void* next = NextObj(start);


		// 找到对应span
		Span* span = PageCache::GetInstance()->MapObjectToSpan(start);
		// 头插
		NextObj(start) = span->_freeList;
		span->_freeList = start;

		// 这里用usecount来判断要不要还给pagecache
		span->_useCount--;
		if (span->_useCount == 0)
		{
			// 可以回收给pagecache了，pagecache再去尝试做前后页的合并
			_spanLists[index].Erase(span);
			span->_freeList = nullptr;
			span->_prev = nullptr;
			span->_next = nullptr;



			// 缓解内存碎片的问题
			_spanLists[index]._mtx.unlock();
			PageCache::GetInstance()->_pageMtx.lock();
			PageCache::GetInstance()->ReleaseSpanToPageCache(span);
			PageCache::GetInstance()->_pageMtx.unlock();
			_spanLists[index]._mtx.lock();

		}
		start = next;
	}
	// 解锁
	_spanLists[index]._mtx.unlock();
}
