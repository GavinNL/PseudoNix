#include <catch2/catch_all.hpp>
#include <catch2/benchmark/catch_benchmark_all.hpp>
#include <fmt/format.h>
#include <ebash/MiniLinux.h>
#include <ebash/SimpleScheduler.h>
#include <ebash/shell.h>
#include <future>

using namespace bl;


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

