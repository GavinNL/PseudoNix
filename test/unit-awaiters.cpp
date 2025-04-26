#include <coroutine>
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <PseudoNix/System.h>
#include <PseudoNix/Shell.h>


using namespace PseudoNix;

SCENARIO("test await_yield")
{
    System M;

    M.setFunction("test", [](System::e_type control) -> System::task_type {
        PSEUDONIX_PROC_START(control);

        std::cout << "Start" << std::endl;
        std::cout << "Yielding 1" << std::endl;
        co_await control->await_yield();
        std::cout << "Resuming 1" << std::endl;


        std::cout << "Yielding 2" << std::endl;
        co_await control->await_yield();
        std::cout << "Resuming 2" << std::endl;

        std::cout << "Finished" << std::endl;
        co_return 0;
    });

    auto p1 = M.spawnProcess({"test"});
    REQUIRE(p1 != 0);
    REQUIRE(p1 != 0xFFFFFFFF);


    // When we execute, there wont be any references to the
    // input stream that the process is reading from
    // so once the data from that stream is empy
    // it will return EOF
    while(M.executeTaskQueue());
    //REQUIRE(1 == M.executeAll() );
    //REQUIRE(1 == M.executeAll() );
    //REQUIRE(1 == M.executeAll() );

}

#if 0
SCENARIO("test await_read_line")
{
    System M;

    M.setFunction("test", [](System::e_type control) -> System::task_type {
        PSEUDONIX_PROC_START(control);

        std::string line;

        {
            auto v = co_await control->await_read_line(control->in, line);
            REQUIRE(v == PseudoNix::AwaiterResult::SUCCESS);
            REQUIRE(line == "this is a test");
            line.clear();
        }

        {
            auto v = co_await control->await_read_line(control->in, line);
            REQUIRE(v == PseudoNix::AwaiterResult::SUCCESS);
            REQUIRE(line == "this is another test");
            line.clear();
        }

        {
            auto v = co_await control->await_read_line(control->in, line);
            REQUIRE(v == PseudoNix::AwaiterResult::END_OF_STREAM);
        }
        COUT << "Exited gracefully\n";
        co_return 0;
    });


    auto p1 = M.spawnProcess({"test"});
    REQUIRE(p1 != 0);
    REQUIRE(p1 != 0xFFFFFFFF);

    *M.getIO(p1).first << std::string("this is a test\n");
    *M.getIO(p1).first << std::string("this is another test\n");

    // When we execute, there wont be any references to the
    // input stream that the process is reading from
    // so once the data from that stream is empy
    // it will return EOF
    REQUIRE(0 == M.executeAll() );

}

#endif
