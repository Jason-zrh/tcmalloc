#include "ConcurrentAlloc.h"

void Alloc1()
{
    for (size_t i = 0; i < 5; i++)
    {
        void* ptr = ConcurrentAlloc(6);
    }
}

void Alloc2()
{
    for (size_t i = 0; i < 5; i++)
    {
        void* ptr = ConcurrentAlloc(7);
    }
}

// thread cache 
void TLSTest()
{
    std::thread t1(Alloc1);
    t1.join();
    std::thread t2(Alloc2);
    t2.join();
}

int main()
{
    TLSTest();
    return 0;
}