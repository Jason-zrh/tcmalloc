# Tcmalloc
## 项目简介
  当前项⽬是实现⼀个⾼并发的内存池，他的原型是google的⼀个开源项⽬tcmalloc，tcmalloc全称
Thread-Caching Malloc，即线程缓存的malloc，实现了⾼效的多线程内存管理，⽤于替代系统的内
存分配相关的函数（malloc、free）。
  这个项⽬是把tcmalloc最核⼼的框架简化后拿出来，模拟实现出⼀个⾃⼰的⾼并发内存池，⽬的
就是学习tcamlloc的精华。

## 三大板块
- Thread Cache(线程缓存)
- Central Cache(中心缓存)
- Page Cache(页缓存)

## Thread Cache
  线程缓存最大是256kb
  如果一个字节对齐一次的话将会有20w+自由链表，所以我们采用下面的对齐方式
  这种对齐方式可以做到10%左右的内碎片浪费
  | 对齐方式 | 申请内存块大小范围 | 自由链表的下标 |
  |---------|-------------------|--------------|
  | 8bits | [1, 128] | [0, 16) |
  | 16bits | [128 + 1, 1024] | [16, 72) |
  | 128bits | [1024 + 1, 8 * 1024] | [72, 128) |
  | 1024bits | [8 * 1024 + 1, 64 * 1024] | [128, 184) |
  | 8 * 1024bits | [64 * 1024 + 1, 256 * 1024] | [184, 208) |

  想要申请缓存需要做两件事，一个是内存对齐(如一个对象的大小是9byte， 经过对齐后应该是16byte)，另一个是计算桶的下标(仍然拿9byte举例子， 对齐后的是16byte，所以应该放在第二个桶中，对应的下标则为1， 如果一个对象是17byte则对齐到24byte, 放在第三个桶中，下标为2)。
  下面写出内存对齐和下标计算的核心代码
  ``` cpp
    // 内存对齐
    static size_t _RoundUp(size_t size, size_t alignNum)
    {
        return (size + alignNum - 1) & ~(alignNum - 1);
    }

    // 下标计算
    static inline size_t _Index(size_t byte, size_t alignNum_shift)
    {
        return ((byte + (1 << alignNum_shift) - 1) >> alignNum_shift) - 1;
    }
```
  每次线程需要内存的时候，应该先去_freeLists[index]位置找看看有没有被释放的内存块，如果没有的话则应该去向下一层Central Cache申请
  ``` cpp
  // ThreadCache申请size大小的内存
  void* ThreadCache::Allocate(size_t size)
  {
      assert(size <= MAX_BYTES);
      // 先对齐内存, 计算应该给size什么对齐方式
      size_t alginSize = SizeClass::RoundUp(size);
      // 计算自由链表中桶的位置
      size_t index = SizeClass::Index(alginSize);
  
      // 自由链表中不为空，直接取一个内存对象返回
      if (!_freeLists[index].Empty())
      {
          return _freeLists[index].Pop();
      }
      // 自由链表为空，从CentralCaChe申请内存对象
      else
      {
          return FetchFromCentralCache(index, alginSize);
      }
  }
```

## Central Cache

