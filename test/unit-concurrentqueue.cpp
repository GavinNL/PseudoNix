#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

//#include <PseudoNix/System.h>
//#include <PseudoNix/Shell.h>
#include <concurrentqueue.h>

#include <thread>

SCENARIO("Test join")
{
    moodycamel::ConcurrentQueue<int> q;
    for(int i=0;i<10000;i++)
        q.enqueue(i);

    auto th = [&q](std::stop_token stoken)
    {
        int item;
        while(!stoken.stop_requested())
        {
            bool found = q.try_dequeue(item);
            (void)found;
            if(found)
            {
                std::cout << std::this_thread::get_id() << ": item" << std::endl;
                //assert(item == 25);
            }

        }
    };

    std::jthread j1(th);
    std::jthread j2(th);
    std::jthread j3(th);

//    int item;
//    while(true)
//    {
//        bool found = q.try_dequeue(item);
//        if(!found)
//            break;
//
//        assert(item == 25);
//    }

}



