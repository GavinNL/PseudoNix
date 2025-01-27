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

MiniLinux::task_type shell(MiniLinux::Exec exev, SimpleScheduler & S, MiniLinux & L)
{
    bool quoted = false;

    std::vector<MiniLinux::Exec> E;

    char last_char =' ';
    auto next_arg  = [&](){if(E.back().args.back().size() != 0) E.back().args.emplace_back();};
    auto next_exec = [&](){E.emplace_back(); E.back().args.emplace_back();};
    auto last_arg = [&]() -> std::string& {return E.back().args.back();};
    auto push_arg_char = [&](char c) {
        last_arg().push_back(c);
        last_char = last_arg().back();
    };


    (void)last_arg;
    next_exec();
    next_arg();


    while(!exev.in->eof() )
    {
        auto c = exev.in->get();
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
                    next_arg();
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
                            //std::cout << "Executing: " << fmt::format("{}", fmt::join(E[j].args, ",")) << std::endl;
                            auto new_task = it->second(E[j]);
                            _returnValues.emplace_back(S.emplace(std::move(new_task)));
                        }
                    }


                    while(true)
                    {
                        auto count = std::count_if(_returnValues.begin(), _returnValues.end(), [](auto & f) { return f.wait_for(std::chrono::seconds(0))==std::future_status::ready;});
                        if(static_cast<size_t>(count) == _returnValues.size())
                        {
                            for(auto & f : _returnValues)
                            {
                                f.get();
                            }
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

    MiniLinux::Exec E;
    E.args = {"sh"};
    E.in = MiniLinux::make_stream("echo hello world\\n ; sleep 10; echo goodbye world;");
    E.out = MiniLinux::make_stream();
    E.in->close();
    auto shell_task = shell(E, S, M);

    S.emplace(std::move(shell_task));
    S.run();
    E.out->toStream(std::cout);
    exit(0);
}


SCENARIO("Test shell222")
{
    std::string cmd = "echo hello world | grep hello ; echo good bye | grep bye;";
    size_t i=0;
    bool quoted = false;

    std::vector<MiniLinux::Exec> E;

    char last_char =' ';
    auto next_arg  = [&](){if(E.back().args.back().size() != 0) E.back().args.emplace_back();};
    auto next_exec = [&](){E.emplace_back(); E.back().args.emplace_back();};
    auto last_arg = [&]() -> std::string& {return E.back().args.back();};
    auto push_arg_char = [&](char c) {
        last_arg().push_back(c);
        last_char = last_arg().back();
    };

    auto execute = [&]()
    {
        std::cout << "Executing:\n";
        {
            for(auto & e : E)
            {
                std::cout << fmt::format("\"{}\"\n", fmt::join(e.args, " "));
            }
            std::cout << std::endl;
            E.clear();
            next_exec();
            next_arg();
        }
    };

    (void)last_arg;
    next_exec();
    next_arg();

    while(i < cmd.size())
    {
        auto c = cmd[i];
        switch(c)
        {
        case '\\':
            ++i;
            c = cmd[i];
            push_arg_char(c);
            break;
        case ' ':
            if(quoted)
                push_arg_char(c);
            else
                next_arg();
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
            execute();
            // execute E
            break;
        default:
            push_arg_char(c);
            break;
        }
        ++i;
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



