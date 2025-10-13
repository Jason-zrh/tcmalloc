#include "PageCache.h"
#include "Common.h"

// 单例模式对象定义
PageCache PageCache:: _sInst;


// 向pagecache要一个k页的span
Span* PageCache::NewSpan(size_t k)
{
	assert(k > 0);
	// 大块内存
	if (k > NPAGES - 1)
	{
		void* ptr = SystemAlloc(k);
		//Span* span = new Span;
		Span* span = _spanPool.New();
		span->_pageId = (PAGE_ID)ptr >> PAGE_SHIFT;
		span->_n = k;
		_idSpanMap[span->_pageId] = span;
		return span;
	}
	// 先判断_spanLists[k]位置是否为空
	// 不为空，从_spanLists给出一个
	if (!_spanLists[k].Empty())
	{
		Span* kSpan  = _spanLists[k].PopFront();
		for (PAGE_ID i = 0; i < kSpan->_n; i++)
		{
			_idSpanMap[kSpan->_pageId + i] = kSpan; //这里是pageId不是_n
		}
		return kSpan;
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
			//Span* kSpan = new Span;
			Span* kSpan = _spanPool.New();
			kSpan->_n = k;
			// 从nSpan的最开始切z
			kSpan->_pageId = nSpan->_pageId;

			nSpan->_pageId += k;
			nSpan->_n -= k;
			// 切好了，需要挂到该在的位置
			_spanLists[nSpan->_n].PushFront(nSpan);


			// 在这里n页的大span也要建立PAGE_ID与span的映射，方便后面进行合并，但是只需要映射它的最开头和最结尾一个页就可以
			_idSpanMap[nSpan->_pageId] = nSpan;
			_idSpanMap[nSpan->_pageId + nSpan->_n - 1] = nSpan;

			// 建立pageid与span的映射，方便回收内存对象
			for (PAGE_ID i = 0; i < kSpan->_n; i++)
			{
				_idSpanMap[kSpan->_pageId + i] = kSpan; //这里是pageId不是_n
			}
			return kSpan;
		}
	}
	// 走到这里是在128页也找不到span，这时候找堆要一个128页的Span
	//Span* bigSpan = new Span;
	Span* bigSpan = _spanPool.New();
	void* ptr = SystemAlloc(NPAGES - 1);
	// 计算页号
	bigSpan->_pageId = ((PAGE_ID)ptr) >> PAGE_SHIFT;
	bigSpan->_n = NPAGES - 1;
	_spanLists[bigSpan->_n].PushFront(bigSpan);
	return NewSpan(k);
}


Span* PageCache::MapObjectToSpan(void* obj)
{
	PAGE_ID id = ((PAGE_ID)obj >> PAGE_SHIFT);

	std::unique_lock<std::mutex> lock(_pageMtx);
	auto ret = _idSpanMap.find(id);
	if (ret != _idSpanMap.end())
	{
		return ret->second;
	}
	else
	{
		assert(false);
		return nullptr;
	}
}


// span还给pagecache的时候，需要找到相邻的页号
void PageCache::ReleaseSpanToPageCache(Span* span)
{
	// 大于128页的span直接还给堆
	if (span->_n > NPAGES - 1)
	{
		void* ptr = (void*)(span->_pageId << PAGE_SHIFT);
		SystemFree(ptr);
		//delete span;
		_spanPool.Delete(span);
		return;
	}
	else
	{
		// 先一直向前合并，一直合并到不能合并
		while (1)
		{
			PAGE_ID prevId = span->_pageId - 1;
			// 先向前找span，直到找不到，前面的页号就是span->_pageId - 1
			auto ret = _idSpanMap.find(prevId);
			if (ret == _idSpanMap.end())
			{
				break;
			}

			Span* prevSpan = ret->second;
			// 判断前一个span是否正在使用
			if (prevSpan->_isUse == true)
			{
				break;
			}
			// 判断两个合起来有没有超过最大页
			if (prevSpan->_n + span->_n >= NPAGES - 1)
			{
				break;
			}

			// 可以开始进行合并
			span->_pageId = prevSpan->_pageId;
			span->_n += prevSpan->_n;
			// 在_spanlist中删除掉，防止野指针
			_spanLists[prevSpan->_n].Erase(prevSpan);
			//delete prevSpan;
			_spanPool.Delete(prevSpan);
		}

		// 再向后合并
		while (1)
		{
			PAGE_ID nextId = span->_pageId + span->_n;
			auto ret = _idSpanMap.find(nextId);
			if (ret == _idSpanMap.end())
			{
				break;
			}
			Span* nextSpan = ret->second;
			if (nextSpan->_isUse == true)
			{
				break;
			}
			if (nextSpan->_n + span->_n >= NPAGES - 1)
			{
				break;
			}
			// 开始向后合并
			span->_n += nextSpan->_n;
			_spanLists[nextSpan->_n].Erase(nextSpan);
			//delete nextSpan;
			_spanPool.Delete(nextSpan);
		}


		// 向前和向后合并结束，把合并好的大块span插到应有的位置
		_spanLists[span->_n].PushFront(span);
		span->_isUse = false;
		// 这里还需要存一下映射关系
		_idSpanMap[span->_pageId] = span;
		_idSpanMap[span->_pageId + span->_n - 1] = span;
	}
}
