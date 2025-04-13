#include <catch2/catch_all.hpp>
#include <catch2/benchmark/catch_benchmark_all.hpp>
#include <ebash/MiniLinux.h>

#include <ebash/shell2.h>

using namespace bl;


SCENARIO("MiniLinux: gen pipeline")
{
    MiniLinux M;

    {
        auto E = MiniLinux::parseArguments( {"X=53", "Y=hello", "echo", "hello"});

        REQUIRE(E.args[0] == "echo");
        REQUIRE(E.args[1] == "hello");
        REQUIRE(E.env.at("X") == "53");
        REQUIRE(E.env.at("Y") == "hello");
    }

    {
        auto E = MiniLinux::parseArguments( {"X=53", "Y=hello"});

        REQUIRE(E.args.size() == 0);
        REQUIRE(E.env.at("X") == "53");
        REQUIRE(E.env.at("Y") == "hello");
    }

    {
        auto E = MiniLinux::parseArguments( {"X=53", "Y=hello", "env", "Z=arg"});

        REQUIRE(E.args.size() == 2);
        REQUIRE(E.args[0] == "env");
        REQUIRE(E.args[1] == "Z=arg");
        REQUIRE(E.env.at("X") == "53");
        REQUIRE(E.env.at("Y") == "hello");
    }
}


SCENARIO("MiniLinux: Run a single command manually")
{
    MiniLinux M;

    // Clear any default commands
    M.m_funcs.clear();

    // add our command manually
    M.m_funcs["echo"] = [](MiniLinux::e_type control) -> MiniLinux::task_type {
        auto & args = *control;
        for (size_t i = 1; i < args.args.size(); i++) {
            *args.out << args.args[i] + (i == args.args.size() - 1 ? "" : " ");
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
    auto pid = M.runRawCommand(exec);
    REQUIRE(pid != 0);
    REQUIRE(pid != 0xFFFFFFFF);

    // We dont have a scheduler, so we'll manually
    // run this until its finished
    while (M.executeAll());

    REQUIRE(exec.out->str() == "hello world");
}

SCENARIO("MiniLinux: Run a single command manually read from input")
{
    MiniLinux M;

    // Clear any default commands
    M.m_funcs.clear();

    // add our command manually
    M.m_funcs["echo_from_input"] = [](MiniLinux::e_type control) -> MiniLinux::task_type {
        auto & args = *control;
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
    auto pid = M.runRawCommand(exec);
    REQUIRE(pid != 0);
    REQUIRE(pid != 0xFFFFFFFF);

    // We dont have a scheduler, so we'll manually
    // run this until its finished
    while (M.executeAll());

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
    REQUIRE(0xFFFFFFFF != M.runRawCommand(exec[0]));
    REQUIRE(0xFFFFFFFF != M.runRawCommand(exec[1]));

    // We now have two tasks and each task may block at anytime
    // because it's a coroutine. So we need to resume() each
    // task in whatever order the scheduler decides (we'll just do it
    // in order) until all the tasks are done.

    // We dont have a scheduler, so we'll manually
    // run this until its finished
    while (M.executeAll());

    REQUIRE(exec[1].out->str() == "dlrow olleH\n");
}





SCENARIO("MiniLinux: sh")
{
    MiniLinux M;
    M.m_funcs["sh"] = std::bind(bl::shell2, std::placeholders::_1, ShellEnv{});

    MiniLinux::Exec sh;
    sh.args = {"sh"};
    sh.in  = std::make_shared<MiniLinux::stream_type>();
    sh.out = std::make_shared<MiniLinux::stream_type>();

    *sh.in << "echo hello world;";
    sh.in->close();
    M.runRawCommand(sh);

    while(M.executeAll());

    REQUIRE(sh.out->str() == "hello world\n");
}


SCENARIO("MiniLinux: sh - multicommand")
{
    MiniLinux M;
    M.m_funcs["sh"] = std::bind(bl::shell2, std::placeholders::_1, ShellEnv{});

    MiniLinux::Exec sh;
    sh.args = {"sh"};
    sh.in  = std::make_shared<MiniLinux::stream_type>();
    sh.out = std::make_shared<MiniLinux::stream_type>();

    *sh.in << "echo hello ; sleep 2.0 ; echo world;";
    sh.in->close();

    REQUIRE(0xFFFFFFFF != M.runRawCommand(sh) );
    while(M.executeAll());

    REQUIRE(sh.out->str() == "hello\nworld\n");
}

SCENARIO("MiniLinux: sh - multicommand with newline")
{
    MiniLinux M;
    M.m_funcs["sh"] = std::bind(bl::shell2, std::placeholders::_1, ShellEnv{});

    MiniLinux::Exec sh;
    sh.args = {"sh"};
    sh.in  = std::make_shared<MiniLinux::stream_type>();
    sh.out = std::make_shared<MiniLinux::stream_type>();

    *sh.in << "echo hello \n sleep 2.0 \n echo world\n";
    sh.in->close();

    REQUIRE(0xFFFFFFFF != M.runRawCommand(sh) );
    while(M.executeAll());

    REQUIRE(sh.out->str() == "hello\nworld\n");
}

SCENARIO("MiniLinux: sh - pipe")
{
    MiniLinux M;
    M.m_funcs["sh"] = std::bind(bl::shell2, std::placeholders::_1, ShellEnv{});
    THEN("We can wr")
    {
        MiniLinux::Exec sh;
        sh.args = {"sh"};
        sh.in  = std::make_shared<MiniLinux::stream_type>();
        sh.out = std::make_shared<MiniLinux::stream_type>();
        *sh.in << "echo hello | wc && echo goodbye;";
        sh.in->close();

        REQUIRE(0xFFFFFFFF != M.runRawCommand(sh) );
        while(M.executeAll());

        REQUIRE(sh.out->str() == "6\ngoodbye\n");
        //sh.out->toStream(std::cout);
        //std::cout << std::endl;
    }
}


