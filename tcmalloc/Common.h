#pragma once
#include <iostream>
#include <vector>
#include <time.h>
#include <assert.h>
#include <thread>

using std::cin;
using std::cout;

// threadcache最大缓存为256kb
static const size_t MAX_BYTES = 256 * 1024;
static const size_t NFREE_LISTS = 208;

// 如果一个字节对齐一次的话将会有20w+自由链表，所以我们采用下面的对齐方式
// 这种对齐方式可以做到10%左右的内碎片浪费
// 对齐方式                  申请内存块大小范围                    自由链表的下标
// ---------------------------------------------------------------------
// 8bits          |      [1, 128]                         |    [0, 16)
// 16bits         |      [128 + 1, 1024]                  |    [16, 72)
// 128bits        |      [1024 + 1, 8 * 1024]             |    [72, 128)
// 1024bits       |      [8 * 1024 + 1, 64 * 1024]        |    [128, 184)
// 8 * 1024bits   |      [64 * 1024 + 1, 256 * 1024]      |    [184, 208) 


class SizeClass
{
public:
    // 内存对齐子函数
    // size_t _RoundUp(size_t size, size_t alignNum) // 参数1为给出的内存大小， 参数2为对齐内存
    // {
    //     size_t alignSize = 0; 
    //     if(size % alignNum != 0)
    //     {
    //         // 需要进行手动对齐
    //         alignSize = (size / alignNum + 1) * alignNum;
    //     }
    //     else
    //     {
    //         // 可以被对齐数整除，说明传进来的size已经自动对齐了
    //         alignSize = size;
    //     }
    //     return alignSize;
    // }

    // 源代码实现方式 -> 位运算，性能更好，效率更高
    static size_t _RoundUp(size_t size, size_t alignNum)
    {
        // 非常巧妙
        return (size + alignNum - 1) & ~(alignNum - 1);
    }


    // 内存对齐实现 
    static inline size_t RoundUp(size_t size)
    {
        if (size <= 128)
        {
            // 8bits对齐方式
            return _RoundUp(size, 8);
        }
        else if (size <= 1024)
        {
            // 16bits对齐方式
            return _RoundUp(size, 16);
        }
        else if (size <= 8 * 1024)
        {
            // 128bits对齐方式
            return _RoundUp(size, 128);
        }
        else if (size <= 64 * 1024)
        {
            // 1024bits对齐方式
            return _RoundUp(size, 1024);
        }
        else if (size <= 256 * 1024)
        {
            // 8 * 1024bits对齐方式
            return _RoundUp(size, 8 * 1024);
        }
        else
        {
            assert(false);
            return -1;
        }
    }


    // 自己实现，性能有点垃圾
    // size_t _Index(size_t size, size_t alignNum)
    // {
    //     if(size % alignNum == 0)
    //     {
    //         return size / alignNum - 1;
    //     }
    //     else
    //     {
    //         return size / alignNum;
    //     }
    // }


    // 源代码实现方式 还是使用位运算
    static inline size_t _Index(size_t byte, size_t alignNum_shift)
    {
        return ((byte + (1 << alignNum_shift) - 1) >> alignNum_shift) - 1;
    }

    static size_t Index(size_t bytes)
    {
        assert(bytes <= MAX_BYTES);
        static int array[4] = { 16, 56, 56, 56 }; // 标记每个区块有多少个链表， 实际上是16，56，56，56，34个自由链表
        if (bytes <= 128)
        {
            // 8bits对齐，在第一个区块,2^3
            return _Index(bytes, 3);
        }
        else if (bytes <= 1024)
        {
            // 16bits对齐，在第二个区块, 2^4
            return _Index(bytes - 128, 4) + array[0];
        }
        else if (bytes <= 8 * 1024)
        {
            // 256bits对齐，在第三个区块, 2^7
            return _Index(bytes - 1024, 7) + array[0] + array[1];

        }
        else if (bytes <= 64 * 1024)
        {
            // 1024bits对齐，在第四个区块, 2^10
            return _Index(bytes - 64 * 1024, 10) + array[0] + array[1] + array[2];

        }
        else if (bytes <= 256 * 1024)
        {
            // 8 * 1024bits对齐，在第五个区块
            return _Index(bytes - 64 * 1024, 13) + array[0] + array[1] + array[2] + array[3];
        }
        else
        {
            assert(false);
            return -1;
        }
    }
};


// 用于取出自由链表前4 / 8个字节
static void*& NextObj(void* obj)
{
    return *((void**)obj);
}


// 自由链表的定义
class FreeList
{
public:


    void Push(void* obj)
    {
        assert(obj);
        // 头插
        NextObj(obj) = _freeList;
        _freeList = obj;
    }

    void* Pop()
    {
        assert(_freeList);
        // 头删
        void* obj = _freeList;
        _freeList = NextObj(obj);
        return obj;
    }

    bool Empty()
    {
        return _freeList == nullptr;
    }
private:
    void* _freeList = nullptr;
};