#pragma once
#include "Common.h"

// 使用基数树优化

// Single-level array(一层基数树)
// 非类型模板参数BITS代表的意思是: 存储页号最大需要多少位。例如x32下 BITS = 32 - PAGE_SHIFT = 19
template <int BITS>
class TCMalloc_PageMap1 
{
private:
    // 1^19
    static const int LENGTH = 1 << BITS;
    // 一层基数树就使用直接定址法，直接开辟LENGTH大小的数组约50w(2MB)
    void** array_;

public:
    typedef uintptr_t Number;

    //explicit TCMalloc_PageMap1(void* (*allocator)(size_t)) 
    explicit TCMalloc_PageMap1()
    {
        //array_ = reinterpret_cast<void**>((*allocator)(sizeof(void*) << BITS));
        // 计算对象大小
        size_t size = sizeof(void*) << BITS;
        // 计算对齐内存
        size_t alignSize = SizeClass::_RoundUp(size, 1 << PAGE_SHIFT);
        // 直接向堆申请内存
        array_ = (void**)SystemAlloc(alignSize >> PAGE_SHIFT);
        memset(array_, 0, sizeof(void*) << BITS);
    }

    // Ensure that the map contains initialized entries "x .. x+n-1".
    // Returns true if successful, false if we could not allocate memory.
    // 保证有空间可以开出这么大的数组
    bool Ensure(Number x, size_t n) 
    {
        // Nothing to do since flat array was allocated at start.  All
        // that's left is to check for overflow (that is, we don't want to
        // ensure a number y where array_[y] would be an out-of-bounds
        // access).
        return n <= LENGTH - x;   // an overflow-free way to do "x + n <= LENGTH"
    }

    void PreallocateMoreMemory() {}

    // Return the current value for KEY.  Returns nullptr if not yet
    // set, or if k is out of range.
    // 相当于hash中的find
    void* get(Number k) const 
    {
        if ((k >> BITS) > 0) 
        {
            return nullptr;
        }
        return array_[k];
    }

    // REQUIRES "k" is in range "[0,2^BITS-1]".
    // REQUIRES "k" has been ensured before.
    //
    // Sets the value 'v' for key 'k'.
    // 相当于给PAGE_ID和Span*建立映射
    void set(Number k, void* v) 
    {
        array_[k] = v;
    }

    // Return the first non-nullptr pointer found in this map for a page
    // number >= k.  Returns nullptr if no such number is found.
    void* Next(Number k) const 
    {
        while (k < (1 << BITS)) 
        {
            if (array_[k] != nullptr) return array_[k];
            k++;
        }
        return nullptr;
    }
};




// Two-level radix tree(两层基数树)
template <int BITS>
class TCMalloc_PageMap2 
{
private:

    static const int ROOT_BITS = 5;
    static const int ROOT_LENGTH = 1 << ROOT_BITS;

    // 14位
    static const int LEAF_BITS = BITS - ROOT_BITS;
    static const int LEAF_LENGTH = 1 << LEAF_BITS;

    // Leaf node
    struct Leaf 
    {
        void* values[LEAF_LENGTH];
    };
    // 开了32个根
    Leaf* root_[ROOT_LENGTH];             // Pointers to child nodes
    void* (*allocator_)(size_t);          // Memory allocator

public:
    typedef uintptr_t Number;

    explicit TCMalloc_PageMap2(void* (*allocator)(size_t)) 
    {
        allocator_ = allocator;
        memset(root_, 0, sizeof(root_));
    }

    
    void* get(Number k) const 
    {
        const Number i1 = k >> LEAF_BITS;
        const Number i2 = k & (LEAF_LENGTH - 1);
        if ((k >> BITS) > 0 || root_[i1] == nullptr) 
        {
            return nullptr;
        }
        return root_[i1]->values[i2];
    }

    void set(Number k, void* v) 
{
        const Number i1 = k >> LEAF_BITS;
        const Number i2 = k & (LEAF_LENGTH - 1);
        ASSERT(i1 < ROOT_LENGTH);
        root_[i1]->values[i2] = v;
    }

    bool Ensure(Number start, size_t n) {
        for (Number key = start; key <= start + n - 1; ) {
            const Number i1 = key >> LEAF_BITS;

            // Check for overflow
            if (i1 >= ROOT_LENGTH)
                return false;

            // Make 2nd level node if necessary
            if (root_[i1] == nullptr) {
                Leaf* leaf = reinterpret_cast<Leaf*>((*allocator_)(sizeof(Leaf)));
                if (leaf == nullptr) return false;
                memset(leaf, 0, sizeof(*leaf));
                root_[i1] = leaf;
            }

            // Advance key past whatever is covered by this leaf node
            key = ((key >> LEAF_BITS) + 1) << LEAF_BITS;
        }
        return true;
    }

    void PreallocateMoreMemory() {
        // Allocate enough to keep track of all possible pages
        if (BITS < 20) {
            Ensure(0, Number(1) << BITS);
        }
    }

    void* Next(Number k) const {
        while (k < (Number(1) << BITS)) {
            const Number i1 = k >> LEAF_BITS;
            Leaf* leaf = root_[i1];
            if (leaf != nullptr) {
                // Scan forward in leaf
                for (Number i2 = k & (LEAF_LENGTH - 1); i2 < LEAF_LENGTH; i2++) {
                    if (leaf->values[i2] != nullptr) {
                        return leaf->values[i2];
                    }
                }
            }
            // Skip to next top-level entry
            k = (i1 + 1) << LEAF_BITS;
        }
        return nullptr;
    }
};

// Three-level radix tree(三层层基数树)
template <int BITS>
class TCMalloc_PageMap3 {
private:
    // How many bits should we consume at each interior level
    static const int INTERIOR_BITS = (BITS + 2) / 3; // Round-up
    static const int INTERIOR_LENGTH = 1 << INTERIOR_BITS;

    // How many bits should we consume at leaf level
    static const int LEAF_BITS = BITS - 2 * INTERIOR_BITS;
    static const int LEAF_LENGTH = 1 << LEAF_BITS;

    // Interior node
    struct Node {
        Node* ptrs[INTERIOR_LENGTH];
    };

    // Leaf node
    struct Leaf {
        void* values[LEAF_LENGTH];
    };

    Node  root_;                          // Root of radix tree
    void* (*allocator_)(size_t);          // Memory allocator

    Node* NewNode() {
        Node* result = reinterpret_cast<Node*>((*allocator_)(sizeof(Node)));
        if (result != nullptr) {
            memset(result, 0, sizeof(*result));
        }
        return result;
    }

public:
    typedef uintptr_t Number;

    explicit TCMalloc_PageMap3(void* (*allocator)(size_t)) {
        allocator_ = allocator;
        memset(&root_, 0, sizeof(root_));
    }

    
    void* get(Number k) const {
        const Number i1 = k >> (LEAF_BITS + INTERIOR_BITS);
        const Number i2 = (k >> LEAF_BITS) & (INTERIOR_LENGTH - 1);
        const Number i3 = k & (LEAF_LENGTH - 1);
        if ((k >> BITS) > 0 ||
            root_.ptrs[i1] == nullptr || root_.ptrs[i1]->ptrs[i2] == nullptr) {
            return nullptr;
        }
        return reinterpret_cast<Leaf*>(root_.ptrs[i1]->ptrs[i2])->values[i3];
    }

    void set(Number k, void* v) {
        ASSERT(k >> BITS == 0);
        const Number i1 = k >> (LEAF_BITS + INTERIOR_BITS);
        const Number i2 = (k >> LEAF_BITS) & (INTERIOR_LENGTH - 1);
        const Number i3 = k & (LEAF_LENGTH - 1);
        reinterpret_cast<Leaf*>(root_.ptrs[i1]->ptrs[i2])->values[i3] = v;
    }

    bool Ensure(Number start, size_t n) {
        for (Number key = start; key <= start + n - 1; ) {
            const Number i1 = key >> (LEAF_BITS + INTERIOR_BITS);
            const Number i2 = (key >> LEAF_BITS) & (INTERIOR_LENGTH - 1);

            // Check for overflow
            if (i1 >= INTERIOR_LENGTH || i2 >= INTERIOR_LENGTH)
                return false;

            // Make 2nd level node if necessary
            if (root_.ptrs[i1] == nullptr) {
                Node* n = NewNode();
                if (n == nullptr) return false;
                root_.ptrs[i1] = n;
            }

            // Make leaf node if necessary
            if (root_.ptrs[i1]->ptrs[i2] == nullptr) {
                Leaf* leaf = reinterpret_cast<Leaf*>((*allocator_)(sizeof(Leaf)));
                if (leaf == nullptr) return false;
                memset(leaf, 0, sizeof(*leaf));
                root_.ptrs[i1]->ptrs[i2] = reinterpret_cast<Node*>(leaf);
            }

            // Advance key past whatever is covered by this leaf node
            key = ((key >> LEAF_BITS) + 1) << LEAF_BITS;
        }
        return true;
    }

    void PreallocateMoreMemory() {
    }

    void* Next(Number k) const {
        while (k < (Number(1) << BITS)) {
            const Number i1 = k >> (LEAF_BITS + INTERIOR_BITS);
            const Number i2 = (k >> LEAF_BITS) & (INTERIOR_LENGTH - 1);
            if (root_.ptrs[i1] == nullptr) {
                // Advance to next top-level entry
                k = (i1 + 1) << (LEAF_BITS + INTERIOR_BITS);
            }
            else {
                Leaf* leaf = reinterpret_cast<Leaf*>(root_.ptrs[i1]->ptrs[i2]);
                if (leaf != nullptr) {
                    for (Number i3 = (k & (LEAF_LENGTH - 1)); i3 < LEAF_LENGTH; i3++) {
                        if (leaf->values[i3] != nullptr) {
                            return leaf->values[i3];
                        }
                    }
                }
                // Advance to next interior entry
                k = ((k >> LEAF_BITS) + 1) << LEAF_BITS;
            }
        }
        return nullptr;
    }
};

