#include <catch2/catch_all.hpp>
#include <catch2/benchmark/catch_benchmark_all.hpp>
#include <fmt/format.h>
#include <ebash/MiniLinux.h>
#include <ebash/SimpleScheduler.h>
#include <future>
#include <ebash/shell.h>

using namespace bl;


SCENARIO("SimpleScheduler")
{
    SimpleScheduler S;
    auto t1 = S.emplace(S.test_task());
    auto t2 = S.emplace(S.test_task());
    auto t3 = S.emplace(S.test_task());
    S.run();
    std::cout << t1.get() << std::endl;
    std::cout << t2.get() << std::endl;
    std::cout << t3.get() << std::endl;
}


SCENARIO("test shell")
{
    {
        auto v = bl::parse_command_line("echo hello world");
        REQUIRE(v.size() == 1);
        REQUIRE(v[0].args.size() == 3);
        REQUIRE(v[0].args[0] == "echo");
        REQUIRE(v[0].args[1] == "hello");
        REQUIRE(v[0].args[2] == "world");
    }
    {
        auto v = bl::parse_command_line("echo hello world | grep world");
        REQUIRE(v.size() == 2);
        REQUIRE(v[0].args.size() == 3);
        REQUIRE(v[0].args[0] == "echo");
        REQUIRE(v[0].args[1] == "hello");
        REQUIRE(v[0].args[2] == "world");

        REQUIRE(v[1].args.size() == 2);
        REQUIRE(v[1].args[0] == "grep");
        REQUIRE(v[1].args[1] == "world");
    }
    exit(0);
}




SCENARIO("MiniLinux: MiniLinux::cmdLineToChain")
{
    MiniLinux M;

    {
        auto chain = MiniLinux::cmdLineToChain("false && echo hello world");

        REQUIRE(chain.size() == 2);
        REQUIRE(chain[0].cmdLine == "false");
        REQUIRE(chain[0].next == MiniLinux::CmdExecList::RUN_NEXT_ON_SUCCESS);
        REQUIRE(chain[1].cmdLine == "echo hello world");
        REQUIRE(chain[1].next == MiniLinux::CmdExecList::RUN_NEXT_ALWAYS);
    }

    {
        auto chain = MiniLinux::cmdLineToChain("false || echo hello world");

        REQUIRE(chain.size() == 2);
        REQUIRE(chain[0].cmdLine == "false");
        REQUIRE(chain[0].next == MiniLinux::CmdExecList::RUN_NEXT_ON_FAIL);
        REQUIRE(chain[1].cmdLine == "echo hello world");
        REQUIRE(chain[1].next == MiniLinux::CmdExecList::RUN_NEXT_ALWAYS);
    }

    {
        auto chain = MiniLinux::cmdLineToChain("echo hello world | grep hello");

        REQUIRE(chain.size() == 2);
        REQUIRE(chain[0].cmdLine == "echo hello world");
        REQUIRE(chain[0].next == MiniLinux::CmdExecList::RUN_NEXT_WITH_PIPED);
        REQUIRE(chain[1].cmdLine == "grep hello");
        REQUIRE(chain[1].next == MiniLinux::CmdExecList::RUN_NEXT_ALWAYS);
    }

    {
        auto chain = MiniLinux::cmdLineToChain("false || echo \"hello && world || goodbye | pipe\" ");

        REQUIRE(chain.size() == 2);
        REQUIRE(chain[0].cmdLine == "false");
        REQUIRE(chain[0].next == MiniLinux::CmdExecList::RUN_NEXT_ON_FAIL);
        REQUIRE(chain[1].cmdLine == "echo \"hello && world || goodbye | pipe\"");
        REQUIRE(chain[1].next == MiniLinux::CmdExecList::RUN_NEXT_ALWAYS);
    }

    {
        auto chain = MiniLinux::cmdLineToChain("echo $(getHomeDir ${USER}) | grep /home && echo good");

        REQUIRE(chain.size() == 3);
        REQUIRE(chain[0].cmdLine == "echo $(getHomeDir ${USER})");
        REQUIRE(chain[0].next == MiniLinux::CmdExecList::RUN_NEXT_WITH_PIPED);
        REQUIRE(chain[1].cmdLine == "grep /home");
        REQUIRE(chain[1].next == MiniLinux::CmdExecList::RUN_NEXT_ON_SUCCESS);
        REQUIRE(chain[2].cmdLine == "echo good");
        REQUIRE(chain[2].next == MiniLinux::CmdExecList::RUN_NEXT_ALWAYS);
    }

    {
        auto chain = MiniLinux::cmdLineToChain("echo hello ; echo world");

        REQUIRE(chain.size() == 2);
        REQUIRE(chain[0].cmdLine == "echo hello");
        REQUIRE(chain[0].next == MiniLinux::CmdExecList::RUN_NEXT_ALWAYS);
        REQUIRE(chain[1].cmdLine == "echo world");
        REQUIRE(chain[1].next == MiniLinux::CmdExecList::RUN_NEXT_ALWAYS);
    }
}

SCENARIO("MiniLinux: Run a single command manually")
{
    MiniLinux M;

    // Clear any default commands
    M.funcs.clear();

    // add our command manually
    M.funcs["echo"] = [](MiniLinux::Exec args) -> MiniLinux::task_type {
        (void) args;
        for (size_t i = 1; i < args.args.size(); i++) {
            *args.out << args.args[i] << (i == args.args.size() - 1 ? "" : " ");
        }
        co_return 0;
    };

    MiniLinux::Exec exec;

    exec.args = {"echo", "hello", "world"};

    // echo will read from this input (stdin)
    exec.in = MiniLinux::make_stream();
    // echo will write to this output stream (stdout)
    exec.out = MiniLinux::make_stream();

    // Make sure we close the input stream otherwise
    // Otherwise if the command reads from input, it will
    // block until data is available or the stream is closed
    exec.in->close();

    //=======================================================
    // Execute the command
    //=======================================================
    auto shell_task = M.runRawCommand(exec);

    // We dont have a scheduler, so we'll manually
    // run this until its finished
    while (!shell_task.done()) {
        shell_task.resume();
    }

    REQUIRE(exec.out->str() == "hello world");
}

SCENARIO("MiniLinux: Run a single command manually read from input")
{
    MiniLinux M;

    // Clear any default commands
    M.funcs.clear();

    // add our command manually
    M.funcs["echo_from_input"] = [](MiniLinux::Exec args) -> gul::Task<int> {
        while (true)
        {
            if (args.in->eof())
                break;

            if (!args.in->has_data())
                co_await std::suspend_always{};

            auto c = args.in->get();
            args.out->put(c);
        }

        co_return 0;
    };

    MiniLinux::Exec exec;

    exec.args = {"echo_from_input"};

    // Create the stream and put some initial data
    exec.in = MiniLinux::make_stream("Hello world");

    // echo will write to this output stream
    exec.out = MiniLinux::make_stream();

    // Make sure we close the input stream otherwise
    // Otherwise if the command reads from input, it will
    // block until data is available or the stream is closed
    exec.in->close();

    //=======================================================
    // This returns a coroutine that needs to be executed in
    // your own scheduler
    //
    //=======================================================
    auto shell_task = M.runRawCommand(exec);

    // We dont have a scheduler, so we'll manually
    // run this until its finished
    while (!shell_task.done()) {
        shell_task.resume();
    }

    REQUIRE(exec.out->str() == "Hello world");
}


SCENARIO("MiniLinux: Execute two commands and have one piped into the other")
{
    MiniLinux M;

    //
    // echo and rev are two commands that are added by default
    //
    // We are going to run the equivelant of the bash command:
    //
    //    echo Hello world | rev
    //
    std::array<MiniLinux::Exec,2> exec;

    exec[0].args = {"echo", "Hello", "world"};
    exec[0].in = MiniLinux::make_stream();
    exec[0].out = MiniLinux::make_stream();
    exec[0].in->close(); // only close the first input straem

    exec[1].args = {"rev"};
    exec[1].in = exec[0].out; // make the output of exec[0] the input of exec[1]
    exec[1].out = MiniLinux::make_stream();

    //=======================================================
    // This returns a coroutine that needs to be executed in
    // your own scheduler
    //
    //=======================================================
    std::array<MiniLinux::task_type, 2> tasks = {
        M.runRawCommand(exec[0]),
        M.runRawCommand(exec[1])
    };

    // We now have two tasks and each task may block at anytime
    // because it's a coroutine. So we need to resume() each
    // task in whatever order the scheduler decides (we'll just do it
    // in order) until all the tasks are done.

    while(true)
    {
        uint32_t count = 0;

        for(size_t i=0;i<tasks.size();i++)
        {
            auto & T = tasks[i];

            // check if the task is done, if it is
            // then get the final return code and
            // close it's output stream
            if(T.done())
            {
                auto retcode = T();
                (void)retcode; // we dont need this
                exec[i].out->close();
                count++;
            }
            else
            {
                T.resume();
            }
        }
        if(count == tasks.size())
            break;
    }

    REQUIRE(exec[1].out->str() == "dlrow olleH");
}



SCENARIO("Test Helper Functions")
{
    // This scenario shows how to use the helper function
    // to perform the previous example
    MiniLinux M;


    // There are 3 helper functions, the third function is just a combination
    // of the first two and should be in all cases. This example of the first
    // two functions are just for unit test purposes
    {
        // We can use cmdLineToChain to generate the chain of execs
        // that need to be run
        //
        auto chain = MiniLinux::cmdLineToChain("echo hello | rev | rev");

        auto in  = MiniLinux::make_stream();
        auto out = MiniLinux::make_stream();

        // executePipedChain means that all commands have to be piped into
        // each other
        //
        // This will return a single task that can be placed onto your
        // coroutine scheduler
        auto T = MiniLinux::executePipedChain(chain,
                                              in,
                                              out,
                                              &M);

        // Execute all commands
        while(!T.done())
        {
            T.resume();
        }

        std::stringstream ss;
        out->toStream(ss);
        REQUIRE(ss.str() == "hello");
    }

}

SCENARIO("Test piped")
{
    MiniLinux M;
    {
        auto chain = MiniLinux::cmdLineToChain("echo hello");

        auto in  = std::make_shared<MiniLinux::stream_type>();
        auto out = std::make_shared<MiniLinux::stream_type>();

        auto T = MiniLinux::executePipedChain(chain, in, out, &M);
        while(!T.done())
        {
            T.resume();
        }

        std::stringstream ss;
        out->toStream(ss);
        REQUIRE(ss.str() == "hello");
    }

    {
        auto chain = MiniLinux::cmdLineToChain("echo hello | rev");

        auto in  = std::make_shared<MiniLinux::stream_type>();
        auto out = std::make_shared<MiniLinux::stream_type>();

        auto T = MiniLinux::executePipedChain(chain, in, out, &M);
        while(!T.done())
        {
            T.resume();
        }

        std::stringstream ss;
        out->toStream(ss);
        REQUIRE(ss.str() == "olleh");
    }

    {
        auto chain = MiniLinux::cmdLineToChain("echo hello | rev | rev");

        auto in  = std::make_shared<MiniLinux::stream_type>();
        auto out = std::make_shared<MiniLinux::stream_type>();

        auto T = MiniLinux::executePipedChain(chain, in, out, &M);
        while(!T.done())
        {
            T.resume();
        }

        std::stringstream ss;
        out->toStream(ss);
        REQUIRE(ss.str() == "hello");
    }
}

SCENARIO("Test non-piped chain")
{
    MiniLinux M;

    {
        auto chain = MiniLinux::cmdLineToChain("echo hello");

        auto in  = std::make_shared<MiniLinux::stream_type>();
        auto out = std::make_shared<MiniLinux::stream_type>();

        auto T = MiniLinux::executeNonPipedChain(chain, in, out, &M);
        while(!T.done())
        {
            T.resume();
        }
        REQUIRE(T() == 0);
        std::stringstream ss;
        out->toStream(ss);
        REQUIRE(ss.str() == "hello");
    }

    {
        auto chain = MiniLinux::cmdLineToChain("true && echo hello");

        auto in  = std::make_shared<MiniLinux::stream_type>();
        auto out = std::make_shared<MiniLinux::stream_type>();

        auto T = MiniLinux::executeNonPipedChain(chain, in, out, &M);
        while(!T.done())
        {
            T.resume();
        }
        REQUIRE(T() == 0);
        std::stringstream ss;
        out->toStream(ss);
        REQUIRE(ss.str() == "hello");
    }

    {
        auto chain = MiniLinux::cmdLineToChain("false && echo hello");

        auto in  = std::make_shared<MiniLinux::stream_type>();
        auto out = std::make_shared<MiniLinux::stream_type>();

        auto T = MiniLinux::executeNonPipedChain(chain, in, out, &M);
        while(!T.done())
        {
            T.resume();
        }
        REQUIRE(T() == 1);
        std::stringstream ss;
        out->toStream(ss);
        REQUIRE(ss.str().empty());
    }

    {
        auto chain = MiniLinux::cmdLineToChain("true && echo hello | rev");

        auto in  = std::make_shared<MiniLinux::stream_type>();
        auto out = std::make_shared<MiniLinux::stream_type>();

        auto T = MiniLinux::executeNonPipedChain(chain, in, out, &M);
        while(!T.done())
        {
            T.resume();
        }
        REQUIRE(T() == 0);
        std::stringstream ss;
        out->toStream(ss);
        REQUIRE(ss.str() == "olleh");
    }

    {
        auto chain = MiniLinux::cmdLineToChain("false && echo hello | rev");

        auto in  = std::make_shared<MiniLinux::stream_type>();
        auto out = std::make_shared<MiniLinux::stream_type>();

        auto T = MiniLinux::executeNonPipedChain(chain, in, out, &M);
        while(!T.done())
        {
            T.resume();
        }
        REQUIRE(T() == 1);
        std::stringstream ss;
        out->toStream(ss);
        REQUIRE(ss.str().empty());
    }

    {
        auto chain = MiniLinux::cmdLineToChain("true && echo hello | rev | rev | rev && echo done");

        auto in  = std::make_shared<MiniLinux::stream_type>();
        auto out = std::make_shared<MiniLinux::stream_type>();

        auto T = MiniLinux::executeNonPipedChain(chain, in, out, &M);
        while(!T.done())
        {
            T.resume();
        }
        REQUIRE(T() == 0);
        std::stringstream ss;
        out->toStream(ss);
        REQUIRE(ss.str() == "ollehdone");
    }

}


#if 0


SCENARIO("MiniLinux: sh")
{
    MiniLinux M;


    MiniLinux::Exec sh;
    sh.args = {"sh"};
    sh.in  = std::make_shared<MiniLinux::stream_type>();
    sh.out = std::make_shared<MiniLinux::stream_type>();

    *sh.in << "echo hello world";
    sh.in->close();
    auto shell_task = M.runRawCommand(sh);
    while(!shell_task.done())
    {
        shell_task.resume();
    }

    REQUIRE(sh.out->str() == "hello world\n");
}


SCENARIO("MiniLinux: sh - multicommand")
{
    MiniLinux M;


    MiniLinux::Exec sh;
    sh.args = {"sh"};
    sh.in  = std::make_shared<MiniLinux::stream_type>();
    sh.out = std::make_shared<MiniLinux::stream_type>();

    *sh.in << "echo hello ; sleep 2.0 ; echo world";
    sh.in->close();
    auto shell_task = M.runRawCommand(sh);
    while(!shell_task.done())
    {
        shell_task.resume();
    }

    REQUIRE(sh.out->str() == "hello\nworld\n");
}

SCENARIO("MiniLinux: sh - multicommand with newline")
{
    MiniLinux M;


    MiniLinux::Exec sh;
    sh.args = {"sh"};
    sh.in  = std::make_shared<MiniLinux::stream_type>();
    sh.out = std::make_shared<MiniLinux::stream_type>();

    *sh.in << "echo hello \n sleep 2.0 \n echo world";
    sh.in->close();
    auto shell_task = M.runRawCommand(sh);
    while(!shell_task.done())
    {
        shell_task.resume();
    }

    REQUIRE(sh.out->str() == "hello\nworld\n");
}

SCENARIO("MiniLinux: sh - pipe")
{
    MiniLinux M;

    THEN("We can wr")
    {
        MiniLinux::Exec sh;
        sh.args = {"sh"};
        sh.in  = std::make_shared<MiniLinux::stream_type>();
        sh.out = std::make_shared<MiniLinux::stream_type>();
        *sh.in << "echo hello | wc && echo goodbye";
        sh.in->close();
        auto shell_task = M.runRawCommand(sh);
        while(!shell_task.done())
        {
            shell_task.resume();
        }
        REQUIRE(sh.out->str() == "6");
        sh.out->toStream(std::cout);
        std::cout << std::endl;
    }
}
#endif



