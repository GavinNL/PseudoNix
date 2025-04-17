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


SCENARIO("Test await_yield")
{
    System M;

    // add our command manually
    M.setFunction("test", [](System::e_type control) -> System::task_type {
        PSEUDONIX_PROC_START(control);

        COUT << "test: wait\n";
        co_await control->await_yield(); // yield at this point, echo will run
        COUT << "test: resume\n";

        co_return 0;
    });

    auto out = System::make_stream();
    auto E1 = System::parseArguments({"test"});
    E1.out = out;
    auto E2 = System::parseArguments({"echo", "echo"});
    E2.out = out;
    auto pid1 = M.runRawCommand(E1);
    auto pid2 = M.runRawCommand(E2);

    REQUIRE(pid1 != 0);
    REQUIRE(pid1 != 0xFFFFFFFF);

    REQUIRE(pid2 != 0);
    REQUIRE(pid2 != 0xFFFFFFFF);

    // We dont have a scheduler, so we'll manually
    // run this until its finished
    while (M.executeAll());

    REQUIRE(out->str() == "test: wait\n"
                           "echo\n"
                           "test: resume\n");
}

SCENARIO("Test await_yield_for")
{
    System M;

    // add our command manually
    M.setFunction("test", [](System::e_type control) -> System::task_type {
        PSEUDONIX_PROC_START(control);

        COUT << "test: wait\n";
        co_await control->await_yield_for(std::chrono::seconds(1)); // yield at this point, echo will run
        COUT << "test: resume\n";

        co_return 0;
    });

    auto out = System::make_stream();
    auto E1 = System::parseArguments({"test"});
    E1.out = out;
    auto E2 = System::parseArguments({"echo", "echo"});
    E2.out = out;
    auto pid1 = M.runRawCommand(E1);
    auto pid2 = M.runRawCommand(E2);

    REQUIRE(pid1 != 0);
    REQUIRE(pid1 != 0xFFFFFFFF);

    REQUIRE(pid2 != 0);
    REQUIRE(pid2 != 0xFFFFFFFF);

    // We dont have a scheduler, so we'll manually
    // run this until its finished
    auto T0 = std::chrono::system_clock::now();
    while (M.executeAll());
    auto T1 = std::chrono::system_clock::now();

    //
    REQUIRE( T1-T0 > std::chrono::seconds(1));
    REQUIRE(out->str() == "test: wait\n"
                          "echo\n"
                          "test: resume\n");
}

SCENARIO("Test await_finished")
{
    System M;

    // add our command manually
    M.setFunction("test", [](System::e_type control) -> System::task_type {
        PSEUDONIX_PROC_START(control);
        System::pid_type pid;

        std::from_chars(ARGS[1].data(), ARGS[1].data() + ARGS[1].size(), pid);

        COUT << "test: wait\n";
        co_await control->await_finished(pid); // yield at this point, echo will run
        COUT << "test: resume\n";

        co_return 0;
    });

    auto out = System::make_stream();
    auto E1 = System::parseArguments({"sleep", "2"});
    E1.out = out;
    auto pid1 = M.runRawCommand(E1);

    auto E2 = System::parseArguments({"test", std::to_string(pid1)});
    E2.out = out;
    auto pid2 = M.runRawCommand(E2);

    REQUIRE(pid1 != 0);
    REQUIRE(pid1 != 0xFFFFFFFF);

    REQUIRE(pid2 != 0);
    REQUIRE(pid2 != 0xFFFFFFFF);

    // We dont have a scheduler, so we'll manually
    // run this until its finished
    auto T0 = std::chrono::system_clock::now();
    while (M.executeAll());
    auto T1 = std::chrono::system_clock::now();

    //
    REQUIRE( T1-T0 > std::chrono::seconds(2));
    REQUIRE(out->str() == "test: wait\n"
                          "test: resume\n");
}


SCENARIO("Test await_finished multi")
{
    System M;

    // add our command manually
    M.setFunction("test", [](System::e_type control) -> System::task_type {
        PSEUDONIX_PROC_START(control);
        std::vector<System::pid_type> pids(2);


        std::from_chars(ARGS[1].data(), ARGS[1].data() + ARGS[1].size(), pids[0]);
        std::from_chars(ARGS[1].data(), ARGS[1].data() + ARGS[1].size(), pids[1]);

        COUT << "test: wait\n";
        co_await control->await_finished(pids); // yield at this point, echo will run
        COUT << "test: resume\n";

        co_return 0;
    });

    auto out = System::make_stream();

    auto E1 = System::parseArguments({"sleep", "2"});
    E1.out = out;
    auto pid1 = M.runRawCommand(E1);
    auto E2 = System::parseArguments({"sleep", "2"});
    E2.out = out;
    auto pid2 = M.runRawCommand(E2);

    auto E3 = System::parseArguments({"test", std::to_string(pid1), std::to_string(pid2)});
    E3.out = out;
    auto pid3 = M.runRawCommand(E3);

    REQUIRE(pid1 != 0);
    REQUIRE(pid1 != 0xFFFFFFFF);

    REQUIRE(pid2 != 0);
    REQUIRE(pid2 != 0xFFFFFFFF);

    REQUIRE(pid3 != 0);
    REQUIRE(pid3 != 0xFFFFFFFF);

    // We dont have a scheduler, so we'll manually
    // run this until its finished
    auto T0 = std::chrono::system_clock::now();
    while (M.executeAll());
    auto T1 = std::chrono::system_clock::now();

    //
    // Still only takes 2 seconds since both sleeps are in parallel
    REQUIRE( T1-T0 > std::chrono::seconds(2));
    REQUIRE(out->str() == "test: wait\n"
                          "test: resume\n");
}


SCENARIO("test await_data")
{
    System M;

    // add our command manually
    M.setFunction("test", [](System::e_type control) -> System::task_type {
        PSEUDONIX_PROC_START(control);

        char c;
        COUT << "test: wait 1\n";
        co_await control->await_has_data(control->in);
        COUT << "test: resume 1\n";
        REQUIRE(control->in->get(&c) == System::stream_type::Result::SUCCESS);
        REQUIRE(c == '1');

        COUT << "test: wait 2\n";
        co_await control->await_has_data(control->in);
        COUT << "test: resume 2\n";
        REQUIRE(control->in->get(&c) == System::stream_type::Result::SUCCESS);
        REQUIRE(c == '2');

        REQUIRE(AwaiterResult::SUCCESS == co_await control->await_has_data(control->in));
        REQUIRE(control->in->eof()==true);
        //REQUIRE(control->in->get(&c) == System::stream_type::Result::SUCCESS);
        co_return 0;
    });

    auto E1 = System::parseArguments({"test"});
    E1.out = System::make_stream();
    E1.in  = System::make_stream();

    auto pid1 = M.runRawCommand(E1);
    REQUIRE(pid1 != 0);
    REQUIRE(pid1 != 0xFFFFFFFF);


    // Execute once until the first yield
    REQUIRE(1 == M.executeAll() );

    // place something in the stream
    // and execute again
    E1.in->put('1');
    REQUIRE(1 == M.executeAll() );

    // place soemthing again and execute
    E1.in->put('2');
    REQUIRE(1 == M.executeAll() );

    E1.in->set_eof();
    REQUIRE(0 == M.executeAll() );

    //
    // Still only takes 2 seconds since both sleeps are in parallel
    REQUIRE(E1.out->str() == "test: wait 1\n"
                             "test: resume 1\n"
                             "test: wait 2\n"
                             "test: resume 2\n"
            );
}


SCENARIO("Test signal")
{
    System M;


    auto pid = M.spawnProcess({"sleep", "50"});
    REQUIRE(pid != 0);
    REQUIRE(pid != 0xFFFFFFFF);

    // Execute once until the first yield
    REQUIRE(1 == M.executeAll() );

    M.signal(pid, sig_interrupt);

    REQUIRE(0 == M.executeAll() );
}


SCENARIO("Test kill")
{
    System M;

    M.setFunction("test", [](System::e_type control) -> System::task_type {
        PSEUDONIX_PROC_START(control);

        PSEUDONIX_TRAP
        {
            COUT << std::format("onExit\n");
        };

        while(true)
        {
            // breaks the loop if await yields a non-success value
            HANDLE_AWAIT_BREAK_ON_SIGNAL(co_await control->await_yield(), control);
        }
        COUT << "Exited gracefully\n";
        co_return 0;
    });


    auto p1 = M.spawnProcess({"test"});
    REQUIRE(p1 != 0);
    REQUIRE(p1 != 0xFFFFFFFF);

    auto p2 = M.spawnProcess({"test"});
    REQUIRE(p2 != 0);
    REQUIRE(p2 != 0xFFFFFFFF);

    // execute once, should yield
    REQUIRE(2 == M.executeAll() );

    // send sig-interrupt. p1 should exit
    // after next run
    {
        auto O1 = M.getIO(p1).second;

        M.signal(p1, sig_interrupt);
        REQUIRE(1 == M.executeAll() );
        REQUIRE(M.isRunning(p1) == false); // p1 is no longer running
        // The interrupt handled
        REQUIRE(
        O1->str() == "Exited gracefully\n"
                     "onExit\n"
            );
    }


    // force kill P2
    {
        auto O2 = M.getIO(p2).second;

        M.kill(p2);
        REQUIRE(0 == M.executeAll() );
        REQUIRE(M.isRunning(p2) == false); // p1 is no longer running
        // The interrupt handled
        // Did not exit gracefully, but defer block was called
        REQUIRE( O2->str() == "onExit\n" );
    }

}

