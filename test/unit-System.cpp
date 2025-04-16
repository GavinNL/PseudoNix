#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <PseudoNix/System.h>
#include <PseudoNix/Shell.h>

using namespace PseudoNix;

SCENARIO("Test join")
{
    std::vector vec = {1,2,3,4};
    REQUIRE(std::format("{}", join(vec)) == "1, 2, 3, 4");
}

SCENARIO("Test SplitVar")
{
    {
        auto [var, val] = splitVar("Var=Value");
        REQUIRE(var == "Var");
        REQUIRE(val == "Value");
    }
    {
        auto [var, val] = splitVar("Var Value");
        REQUIRE(var.empty());
        REQUIRE(val.empty());
    }
}

SCENARIO("Test parseArguments")
{
    {
        auto E = System::parseArguments( {"X=53", "Y=hello", "echo", "hello"});

        REQUIRE(E.args[0] == "echo");
        REQUIRE(E.args[1] == "hello");
        REQUIRE(E.env.at("X") == "53");
        REQUIRE(E.env.at("Y") == "hello");
    }

    {
        auto E = System::parseArguments( {"X=53", "Y=hello"});

        REQUIRE(E.args.size() == 0);
        REQUIRE(E.env.at("X") == "53");
        REQUIRE(E.env.at("Y") == "hello");
    }

    {
        auto E = System::parseArguments( {"X=53", "Y=hello", "env", "Z=arg"});

        REQUIRE(E.args.size() == 2);
        REQUIRE(E.args[0] == "env");
        REQUIRE(E.args[1] == "Z=arg");
        REQUIRE(E.env.at("X") == "53");
        REQUIRE(E.env.at("Y") == "hello");
    }
}

SCENARIO("Test genPipeline")
{
    auto E = System::genPipeline({
        {"X=53", "Y=hello", "echo", "hello"},
        {"rev"}
    });

    REQUIRE(E[0].args[0] == "echo");
    REQUIRE(E[0].args[1] == "hello");
    REQUIRE(E[0].env.at("X") == "53");
    REQUIRE(E[0].env.at("Y") == "hello");

    REQUIRE(E[1].args[0] == "rev");
}

SCENARIO("Return values")
{
    GIVEN("An initial system") {

        System S;

        WHEN("We execute a process which has 0 exit code") {

            System::Exec exec;
            exec.args = {"true"};
            exec.in = System::make_stream();
            exec.out = System::make_stream();
            exec.in->set_eof();

            auto pid = S.runRawCommand(exec);

            THEN("The PID returns non-zero") {
                REQUIRE(pid != 0xFFFFFFFF);
                REQUIRE(pid != 0);
            }

            WHEN("We run the system") {

                auto re = S.getProcessExitCode(pid);
                REQUIRE(*re == -1);
                while(S.executeAll());
                REQUIRE(*re == 0);
            }
        }
        WHEN("We execute a process which has 1 exit code") {

            System::Exec exec;
            exec.args = {"false"};
            exec.in = System::make_stream();
            exec.out = System::make_stream();
            exec.in->set_eof();

            auto pid = S.runRawCommand(exec);

            THEN("The PID returns non-zero") {
                REQUIRE(pid != 0xFFFFFFFF);
                REQUIRE(pid != 0);
            }

            WHEN("We run the system") {

                auto re = S.getProcessExitCode(pid);
                REQUIRE(*re == -1);
                while(S.executeAll());
                REQUIRE(*re == 1);
            }
        }
    }
}

SCENARIO("runRawCommand")
{
    GIVEN("An initial system") {

        System S;

        WHEN("We execute a process") {

            System::Exec exec;
            exec.args = {"echo", "-n", "hello", "world"};
            exec.in = System::make_stream();
            exec.out = System::make_stream();
            exec.in->set_eof();

            auto pid = S.runRawCommand(exec);

            THEN("The PID returns non-zero") {
                REQUIRE(pid != 0xFFFFFFFF);
                REQUIRE(pid != 0);
            }

            WHEN("We run the system") {

                auto re = S.getProcessExitCode(pid);
                REQUIRE(*re == -1);

                while(S.executeAll());

                REQUIRE(exec.out->str() == "hello world");
                REQUIRE(*re == 0);
            }
        }
    }
}

SCENARIO("System: Run a single command manually read from input")
{
    System M;

    // Clear any default commands
    M.removeAllFunctions();

    // add our command manually
    M.setFunction("echo_from_input", [](System::e_type control) -> System::task_type {
        auto & args = *control;
        char c=0;
        while (true)
        {
            if(System::stream_type::Result::SUCCESS == args.in->get(&c))
            {
                args.out->put(c);
                co_await std::suspend_always{};
            }
            else
            {
                break;
            }
        }

        co_return 0;
    });

    System::Exec exec;

    exec.args = {"echo_from_input"};

    // Create the stream and put some initial data
    exec.in = System::make_stream("Hello world");
    exec.out = System::make_stream();
    exec.in->set_eof();


    auto pid = M.runRawCommand(exec);
    REQUIRE(pid != 0);
    REQUIRE(pid != 0xFFFFFFFF);

    // We dont have a scheduler, so we'll manually
    // run this until its finished
    while (M.executeAll());

    REQUIRE(exec.out->str() == "Hello world");
}



SCENARIO("System: Execute two commands and have one piped into the other")
{
    System M;

    //
    // echo and rev are two commands that are added by default
    //
    // We are going to run the equivelant of the bash command:
    //
    //    echo Hello world | rev
    //
    std::array<System::Exec,2> exec;

    exec[0].args = {"echo", "-n", "Hello", "world"};
    exec[0].in = System::make_stream();
    exec[0].out = System::make_stream();
    exec[0].in->set_eof(); // only close the first input straem

    exec[1].args = {"rev"};
    exec[1].in = exec[0].out; // make the output of exec[0] the input of exec[1]
    exec[1].out = System::make_stream();

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
