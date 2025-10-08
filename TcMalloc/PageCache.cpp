#include "PageCache.h"
#include "Common.h"

// 单例模式对象定义
PageCache PageCache:: _sInst;


Span* PageCache::NewSpan(size_t k)
{
	assert(k > 0);
	assert(k < NPAGES);
	// 先判断SpanList[k]位置是否为空
	// 不为空，从_spanLists给出一个
	if (!_spanLists[k].Empty())
	{
		return _spanLists[k].PopFront();
	}
	// 这个位置为空，去遍历后面的桶有没有有span的
	for (int i = k + 1; i < NPAGES; i++)
	{
		// 有一个span不为空
		if (!_spanLists[i].Empty())
		{
			// 先把它拿出来
			Span* nSpan = _spanLists[i].PopFront();
			// 进行切分，切成两个，一个k页的span和一个n - k页的span
			Span* kSpan = new Span;
			kSpan->_n = k;
			kSpan->_pageId = nSpan->_pageId;

			nSpan->_pageId += k;
			nSpan->_n -= k;
			// 切好了，需要挂到该在的位置
			_spanLists[nSpan->_n].PushFront(nSpan);
			return kSpan;
		}
	}
	// 走到这里是在128页也找不到span，这时候找堆要一个128页的Span
	Span* bigSpan = new Span;
	void* ptr = SystemAlloc(NPAGES - 1);
	bigSpan->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
	bigSpan->_n = NPAGES - 1;
	_spanLists[bigSpan->_n].PushFront(bigSpan);
	return NewSpan(k);
}
