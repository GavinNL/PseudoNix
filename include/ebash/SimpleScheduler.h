#ifndef MINILINUX_SIMPLE_SCHEDULER_H
#define MINILINUX_SIMPLE_SCHEDULER_H

#include <map>
#include <future>
#include "MiniLinux.h"

namespace bl
{
struct SimpleScheduler
{
    std::map< size_t, std::pair<std::promise<int>, MiniLinux::task_type > > _tasks;
    size_t _proc=1;
    std::future<int> emplace(MiniLinux::task_type && t)
    {
        std::pair<std::promise<int>, MiniLinux::task_type > _t = { std::promise<int>(), std::move(t)};
        _tasks.emplace(_proc++, std::move(_t));
        return _tasks.at(_proc-1).first.get_future();
    }
    size_t run_once()
    {
        for(auto it = _tasks.begin(); it != _tasks.end(); )
        {
            if(!it->second.second.done())
            {
                it->second.second.resume();
            }
            auto _done = it->second.second.done();
            if(_done)
            {
                it->second.first.set_value(it->second.second());
                it = _tasks.erase(it);
            }
            else
            {
                ++it;
            }
        }
        return _tasks.size();
    }

    void run()
    {
        while(run_once())
        {
        }
    }

    static MiniLinux::task_type test_task()
    {
        for(size_t i=0;i<10;i++)
        {
            std::cout << i << std::endl;
            co_await std::suspend_always{};
        }
        std::cout << "Done" << std::endl;
        co_return 0;
    }
};


}
#endif
