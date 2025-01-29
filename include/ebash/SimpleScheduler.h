#ifndef MINILINUX_SIMPLE_SCHEDULER_H
#define MINILINUX_SIMPLE_SCHEDULER_H

#include <map>
#include <future>
#include "MiniLinux.h"

namespace bl
{

/**
 * @brief The SimpleScheduler class
 *
 * This SimpleScheduler holds all the coroutine tasks
 * in a map and executes them in order when the scheduler
 * is invoked
 */
struct SimpleScheduler
{
    struct proc
    {
        std::promise<int>    returnCode_promise;
        MiniLinux::task_type task;

        // optional
        MiniLinux::Exec      exec;
    };

    std::map< size_t, proc > _tasks;
    size_t _proc=1;

    /**
     * @brief emplace
     * @param t
     * @return
     *
     */
    std::future<int> emplace(MiniLinux::task_type && t)
    {
        proc _t = { std::promise<int>(), std::move(t)};
        _tasks.emplace(_proc++, std::move(_t));
        return _tasks.at(_proc-1).returnCode_promise.get_future();
    }

    std::future<int> emplace_process(bl::MiniLinux::task_type && _task, bl::MiniLinux::Exec E)
    {
        auto f = this->emplace(std::move(_task));
        auto proc = _proc-1;
        _tasks.at(proc).exec = E;
        return f;
    }

    /**
     * @brief run_once
     * @return
     *
     * Execute all the coroutines once and returns the number
     * of coroutines currently in the list.
     *
     */
    size_t run_once()
    {
        for(auto it = _tasks.begin(); it != _tasks.end(); )
        {
            if(!it->second.task.done())
            {
                it->second.task.resume();
            }
            auto _done = it->second.task.done();
            if(_done)
            {
                it->second.returnCode_promise.set_value(it->second.task());
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
        while(run_once());
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
