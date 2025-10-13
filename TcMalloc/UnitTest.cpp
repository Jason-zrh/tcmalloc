#include "ConcurrentAlloc.h"

//// 测试TLS
//void Alloc1()
//{
//    for (size_t i = 0; i < 5; i++)
//    {
//        void* ptr = ConcurrentAlloc(6);
//    }
//}
//void Alloc2()
//{
//    for (size_t i = 0; i < 5; i++)
//    {
//        void* ptr = ConcurrentAlloc(7);
//    }
//}
//// thread cache 
//void TLSTest()
//{
//    std::thread t1(Alloc1);
//    t1.join();
//    cout << std::endl;
//    std::thread t2(Alloc2);
//    t2.join();
//}
//
//// 测试申请流程
//void TestConcurrentAlloc()
//{
//    void* p1 = ConcurrentAlloc(6);
//    void* p2 = ConcurrentAlloc(2);
//    void* p3 = ConcurrentAlloc(1);
//    void* p4 = ConcurrentAlloc(7);
//    void* p5 = ConcurrentAlloc(5);
//    void* p6 = ConcurrentAlloc(8);
//
//    cout << p1 << endl;
//    cout << p2 << endl;
//    cout << p3 << endl;
//    cout << p4 << endl;
//    cout << p5 << endl;
//    cout << p6 << endl;
//}
//
//// 测试释放流程
//void TestConcurrentFree()
//{
//    void* p1 = ConcurrentAlloc(6);
//    void* p2 = ConcurrentAlloc(2);
//    void* p3 = ConcurrentAlloc(1);
//    void* p4 = ConcurrentAlloc(7);
//    void* p5 = ConcurrentAlloc(5);
//    void* p6 = ConcurrentAlloc(8);
//
//    cout << p1 << endl;
//    cout << p2 << endl;
//    cout << p3 << endl;
//    cout << p4 << endl;
//    cout << p5 << endl;
//    cout << p6 << endl;
//
//    ConcurrentFree(p1);
//    ConcurrentFree(p2);
//    ConcurrentFree(p3);
//    ConcurrentFree(p4);
//    ConcurrentFree(p5);
//    ConcurrentFree(p6);
//}
//
//
//// 测试大块内存申请释放逻辑
//void BigAlloc()
//{
//    // 大于threadcache但是小于128 * 8 * 1024
//    void* p1 = ConcurrentAlloc(257 * 1024);
//    ConcurrentFree(p1);
//
//    void* p2 = ConcurrentAlloc(129 * 8 * 1024);
//    ConcurrentFree(p2);
//}
//
//int main()
//{
//    //TLSTest();
//    //TestConcurrentAlloc();
//    TestConcurrentFree();
//    //BigAlloc();
//
//    return 0;
//}


