#include <catch2/catch_all.hpp>
#include <catch2/benchmark/catch_benchmark_all.hpp>
#include <fmt/format.h>
#include <ebash/MiniLinux.h>
#include <future>

using namespace bl;

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

};



/**
 * @brief shell
 * @param exev
 * @param S
 * @param L
 * @return
 *
 * The shell() command acts like the Linux's sh command. It reads commands
 * from the input stream and executes commands.
 *
 * Each command that get excectued is itself a coroutine. These coroutine tasks
 * will need to be placed on a scheduler. That's what the SchedulerFunction does.
 *
 * The SchedulerFunction is a functional object with opertor() overriden
 * It takes a
 */
template<typename SchedulerFunction>
MiniLinux::task_type shell(MiniLinux::Exec exev, SchedulerFunction S, MiniLinux & L)
{
    bool quoted = false;

    std::vector<MiniLinux::Exec> E;

    char last_char = 0;
    auto next_arg  = [&](){if(E.back().args.back().size() != 0) E.back().args.emplace_back();};
    auto next_exec = [&](){E.emplace_back(); E.back().args.emplace_back();};
    auto last_arg = [&]() -> std::string& {return E.back().args.back();};
    auto push_arg_char = [&](char c) {
        last_arg().push_back(c);
        last_char = c;
    };
    auto pop_char = [&]()
    {
        last_arg().pop_back();
    };
    (void)pop_char;

    (void)last_arg;
    next_exec();
    next_arg();

    while(!exev.in->eof() )
    {
        auto c = exev.in->get();

        if(quoted)
        {
            push_arg_char(c);
        }
        else
        {
            if(c == ' ')
            {
                if(last_char != ' ')
                {
                    next_arg();
                }
            }
            else
            {
                push_arg_char(c);
            }
        }

        switch(c)
        {
            case '\\':
                c = exev.in->get();
                push_arg_char(c);
                break;
            case ' ':
                if(quoted)
                    push_arg_char(c);
                else
                {
                    next_arg();
                }
                break;
            case '"':
                quoted = !quoted;
                break;
            case '|':
                if(!quoted)
                {
                    next_exec();
                }
                break;
            case ';':
            case '\n':
                {
                    E[0].in = exev.in;
                    E.back().out = exev.out;

                    // make sure each executable's output
                    // is passed to the next's input
                    for(size_t j=0;j<E.size()-1;j++)
                    {
                        E[j].out = MiniLinux::make_stream();
                        E[j+1].in = E[j].out;
                    }

                    std::vector<std::future<int>> _returnValues;
                    for(size_t j=0;j<E.size();j++)
                    {
                        auto it = L.funcs.find(E[j].args[0]);
                        if(it != L.funcs.end())
                        {
                            auto new_task = it->second(E[j]);
                            _returnValues.emplace_back(S(std::move(new_task)));
                        }
                    }

                    size_t count=0;
                    while(true)
                    {
                        // check each of the futures for their completion
                        for(size_t i=0;i<_returnValues.size();i++)
                        {
                            auto & f = _returnValues[i];
                            if(f.valid())
                            {
                                if(f.wait_for(std::chrono::seconds(0))==std::future_status::ready)
                                {
                                    ++count;
                                    f.get();
                                    if(E[i].out)
                                        E[i].out->close();
                                }
                            }
                        }

                        if(static_cast<size_t>(count) == _returnValues.size())
                        {
                            break;
                        }
                        else
                        {
                            co_await std::suspend_always{};
                        }
                    }
                    E.clear();
                    next_exec();
                    next_arg();
                }

                // execute E
                break;
            default:
                push_arg_char(c);
                break;
        }
    }
    co_return 1;
}

SCENARIO("test shell")
{
    MiniLinux M;

    SimpleScheduler S;


    // the shell() command acts like linux's sh command, it reads commands from its
    // input and then executes additional commands.
    //
    // The shell() function is a template function that you need to provide a few
    // additional information to.
    //
    // 1. You need to provide it a function that takes a move-only task_type
    //    and returns a std::future<int>. The main goal of the funciton is to place
    //    that task onto your own scheduler, and when that task is completed, set the
    //    future value
    struct DD
    {
        SimpleScheduler * sch;
        std::future<int> operator()(MiniLinux::task_type && _task)
        {
            return sch->emplace(std::move(_task));
        }
    };
    DD d{&S};
    M.funcs["sh"] = std::bind(shell<DD>, std::placeholders::_1, d, M);


    MiniLinux::Exec E;
    E.args = {"sh"};
    //E.out = MiniLinux::make_stream();

    // Here we're going to put our shell script code into the input
    // stream of the process function, similar to how linux works
    E.in  = MiniLinux::make_stream(R"foo(
echo hello world
sleep 2
echo goodbye world | rev
sleep 1
echo hello again
)foo");
    // We're going to close the stream so that the task will complete, otherwise
    // it will keep trying to read code. Eg: you can have std::in pass data into
    // this stream
    E.in->close();

    // finally get the coroutine task and place it
    // into our scheduler
    auto shell_task = M.runRawCommand(E);

    S.emplace(std::move(shell_task));

    // Run the scheduler so that it will
    // continuiously execute the coroutines
    S.run();

    //E.out->toStream(std::cout);
    exit(0);
}

