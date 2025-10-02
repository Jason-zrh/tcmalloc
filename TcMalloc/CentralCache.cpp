#include "CentralCache.h"
#include "PageCache.h"


// 单例模式对象定义
CentralCache CentralCache::_sInst;

Span* CentralCache::GetOneSpan(SpanList& spanlist, size_t size)
{
	// 两种情况：1.是spanlist中有span 2.没有了，去centralCache中申请
	Span* it = spanlist.Begin();
	while (it != spanlist.End())
	{
		if (it->_freeList != nullptr)
		{
			return it;
		}
		else
		{
			it = it->_next;
		}
	}

	// 该找PageCache要了
	Span* span = PageCache::GetInstance()->NewSpan(SizeClass::NumMovePage(size));

	// 已经申请到span了，接下来要把span切好以后挂起来
	// 首先要通过页号算出页的起始地址
	char* start = (char*)(span->_pageId << PAGE_SHIFT);
	// 计算一个span有多少内存
	size_t bytes = span->_n << PAGE_SHIFT;
	char* end = start + bytes;
	// 把大块内存切成自由链表挂起来
	// 先切一块做头，方便尾插
	span->_freeList = start;
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

	spanlist.PushFront(span);

	return span;
}

// threadcache向centralcache批量申请对象，返回值是成功申请对象的个数，前两个参数是输出型参数
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t batchNum, size_t size)
{
	size_t index = SizeClass::Index(size);
	// 先上桶锁
	_spanList[index]._mtx.lock();

	Span* span = GetOneSpan(_spanList[index], size);
	// span取到了，且span里面不为空
	assert(span);
	assert(span->_freeList);

	// 从找的span中取出batchNum个变量
	start = span->_freeList;
	end = start;
	// 这里用end来遍历span
	size_t i = 0;
	size_t actualNum = 1;
	// 从span中获取batchNum个对象，如果不够则有几个拿几个
	while (i < batchNum - 1 && NextObj(end) != nullptr)
	{
		end = NextObj(end);
		i++;
		actualNum++;
	}
	span->_freeList = NextObj(end);
	NextObj(end) = nullptr;

	// 有锁就一定要解锁
	_spanList[index]._mtx.unlock();
	return actualNum;
}



