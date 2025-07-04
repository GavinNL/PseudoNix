#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <PseudoNix/System.h>
#include <PseudoNix/Shell.h>
#include <array>

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
                while(S.taskQueueExecute());
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
                while(S.taskQueueExecute());
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

                while(S.taskQueueExecute());

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
                (void)co_await control->await_yield();
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
    while (M.taskQueueExecute());

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
    std::array<System::Exec, 2> exec;

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
    while (M.taskQueueExecute());

    REQUIRE(exec[1].out->str() == "dlrow olleH\n");
}


SCENARIO("Test await_yield")
{
    System M;

    // add our command manually
    M.setFunction("test", [](System::e_type control) -> System::task_type {
        PN_PROC_START(control);

        COUT << "test: wait\n";
        (void)co_await control->await_yield(); // yield at this point, echo will run
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
    while (M.taskQueueExecute());

    REQUIRE(out->str() == "test: wait\n"
                           "echo\n"
                           "test: resume\n");
}

SCENARIO("Test await_yield_for")
{
    System M;

    // add our command manually
    M.setFunction("test", [](System::e_type control) -> System::task_type {
        PN_PROC_START(control);

        COUT << "test: wait\n";
        (void)co_await control->await_yield_for(std::chrono::seconds(1)); // yield at this point, echo will run
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
    while (M.taskQueueExecute());
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
        PN_PROC_START(control);
        System::pid_type pid;

        std::from_chars(ARGS[1].data(), ARGS[1].data() + ARGS[1].size(), pid);

        COUT << "test: wait\n";
        (void)co_await control->await_finished(pid); // yield at this point, echo will run
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
    while (M.taskQueueExecute());
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
        PN_PROC_START(control);
        std::vector<System::pid_type> pids(2);


        std::from_chars(ARGS[1].data(), ARGS[1].data() + ARGS[1].size(), pids[0]);
        std::from_chars(ARGS[1].data(), ARGS[1].data() + ARGS[1].size(), pids[1]);

        COUT << "test: wait\n";
        (void)co_await control->await_finished(pids); // yield at this point, echo will run
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
    while (M.taskQueueExecute());
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
        PN_PROC_START(control);

        char c;
        COUT << "test: wait 1\n";
        (void)co_await control->await_has_data(control->in);
        COUT << "test: resume 1\n";
        REQUIRE(control->in->get(&c) == System::stream_type::Result::SUCCESS);
        REQUIRE(c == '1');

        COUT << "test: wait 2\n";
        (void)co_await control->await_has_data(control->in);
        COUT << "test: resume 2\n";
        REQUIRE(control->in->get(&c) == System::stream_type::Result::SUCCESS);
        REQUIRE(c == '2');

        REQUIRE(AwaiterResult::END_OF_STREAM == co_await control->await_has_data(control->in));
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
    REQUIRE(1 == M.taskQueueExecute() );

    // place something in the stream
    // and execute again
    E1.in->put('1');
    REQUIRE(1 == M.taskQueueExecute() );

    // place soemthing again and execute
    E1.in->put('2');
    REQUIRE(1 == M.taskQueueExecute() );

    E1.in->set_eof();
    REQUIRE(0 == M.taskQueueExecute() );

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
    REQUIRE(1 == M.taskQueueExecute() );

    M.signal(pid, eSignal::INTERRUPT);

    REQUIRE(0 == M.taskQueueExecute() );
}


SCENARIO("Test kill")
{
    System M;

    M.setFunction("test", [](System::e_type control) -> System::task_type {
        PN_PROC_START(control);

        PSEUDONIX_TRAP
        {
            COUT << std::format("onExit\n");
        };

        while(true)
        {
            // breaks the loop if await yields a non-success value
            PN_HANDLE_AWAIT_BREAK_ON_SIGNAL(co_await control->await_yield(), control);
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
    REQUIRE(2 == M.taskQueueExecute() );

    // send sig-interrupt. p1 should exit
    // after next run
    {
        auto O1 = M.getIO(p1).second;

        M.signal(p1, eSignal::INTERRUPT);
        REQUIRE(1 == M.taskQueueExecute() );
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
        REQUIRE(0 == M.taskQueueExecute() );
        REQUIRE(M.isRunning(p2) == false); // p1 is no longer running
        // The interrupt handled
        // Did not exit gracefully, but defer block was called
        REQUIRE( O2->str() == "onExit\n" );
    }

}


SCENARIO("Test Destroy with processes that dont handle awaits properly")
{
    System M;

    M.setFunction("test_good", [](System::e_type control) -> System::task_type {
        PN_PROC_START(control);

        PSEUDONIX_TRAP
        {
            COUT << std::format("test_good: onExit\n");
        };

        COUT << std::format("test_good: enter loop\n");
        while(true)
        {
            // breaks the loop if await yields a non-success value
            PN_HANDLE_AWAIT_BREAK_ON_SIGNAL(co_await control->await_yield(), control);
        }
        COUT << "test_good: Exited gracefully\n";
        co_return 0;
    });

    M.setFunction("test_bad", [](System::e_type control) -> System::task_type {
        PN_PROC_START(control);

        PSEUDONIX_TRAP
        {
            COUT << std::format("test_bad: onExit\n");
        };

        COUT << std::format("test_bad: enter loop\n");
        while(true)
        {
            // Doesn't handle the signals
            (void)co_await control->await_yield();
        }
        COUT << "test_bad: Exited gracefully\n";
        co_return 0;
    });

    GIVEN("A good and bad process")
    {
        auto p1 = M.spawnProcess({"test_good"});
        REQUIRE(p1 != 0);
        REQUIRE(p1 != 0xFFFFFFFF);

        auto p2 = M.spawnProcess({"test_bad"});
        REQUIRE(p2 != 0);
        REQUIRE(p2 != 0xFFFFFFFF);

        auto O1 = M.getIO(p1).second;
        auto O2 = M.getIO(p2).second;

        REQUIRE(2 == M.taskQueueExecute() );
        WHEN("When we terminateAll()")
        {
            REQUIRE(2 == M.taskQueueExecute() );
            REQUIRE(2 == M.taskQueueExecute() );
            M.terminateAll();

            THEN("Process 1 exits but process 2 does not")
            {
                REQUIRE(1 == M.taskQueueExecute() );
                REQUIRE(M.isRunning(p1) == false);
                REQUIRE(M.isRunning(p2) == true);

                THEN("Process 1 exits gracefully and executes its trap. Process 2 is still running")
                {
                    // The interrupt handled
                    REQUIRE(
                        O1->str() == "test_good: enter loop\n"
                                     "test_good: Exited gracefully\n"
                                     "test_good: onExit\n"
                        );
                    REQUIRE(
                        O2->str() == "test_bad: enter loop\n"
                        );
                }
            }
        }
        WHEN("When we kill p1 and p2")
        {
            REQUIRE(M.kill(p1) == true);
            REQUIRE(M.kill(p2) == true);
            REQUIRE(0 == M.taskQueueExecute() );

            THEN("Both processes stop")
            {
                REQUIRE(M.isRunning(p1) == false);
                REQUIRE(M.isRunning(p2) == false);

                THEN("Neither process exits gracefully. Both execute their trap")
                {
                    // The interrupt handled
                    REQUIRE(
                        O1->str() == "test_good: enter loop\n"
                                     "test_good: onExit\n"
                        );
                    REQUIRE(
                        O2->str() == "test_bad: enter loop\n"
                                     "test_bad: onExit\n"
                        );
                }
            }
        }
        WHEN("When we call destroy()")
        {
            REQUIRE(0 == M.destroy());

            THEN("Both processes stop")
            {
                REQUIRE(M.isRunning(p1) == false);
                REQUIRE(M.isRunning(p2) == false);

                THEN("P1 executes gracefully, but P2 does not")
                {
                    // The interrupt handled
                    REQUIRE(
                        O1->str() == "test_good: enter loop\n"
                                     "test_good: Exited gracefully\n"
                                     "test_good: onExit\n"
                        );
                    REQUIRE(
                        O2->str() == "test_bad: enter loop\n"
                                     "test_bad: onExit\n"
                        );
                }
            }
        }
    }
}


SCENARIO("Test Destroy with processes that dont handle awaits properly")
{
    System M;


    M.setFunction("test_very_bad", [](System::e_type control) -> System::task_type {
        PN_PROC_START(control);

        PSEUDONIX_TRAP
        {
            COUT << std::format("test_very_bad: onExit\n");
        };

        COUT << std::format("test_very_bad: enter loop\n");
        while(true)
        {
            // bad! dont do this. suspends always
            // no way to wake up
            co_await std::suspend_always{};
        }
        COUT << "test_very_bad: Exited gracefully\n";
        co_return 0;
    });

    GIVEN("A very_bad process that co_await std::suspend_always{} ")
    {
        auto p1 = M.spawnProcess({"test_very_bad"});
        REQUIRE(p1 != 0);
        REQUIRE(p1 != 0xFFFFFFFF);

        auto O1 = M.getIO(p1).second;

        REQUIRE(1 == M.taskQueueExecute() );

        WHEN("When we terminateAll()")
        {
            M.terminateAll();
            REQUIRE(1 == M.taskQueueExecute() );

            THEN("The process is still running")
            {
                REQUIRE(M.isRunning(p1) == true);

                THEN("Process 1 exits gracefully and executes its trap. Process 2 is still running")
                {
                    // The interrupt handled
                    REQUIRE(
                        O1->str() == "test_very_bad: enter loop\n"
                        );
                }
            }
        }
        WHEN("When we kill p1")
        {
            REQUIRE(M.kill(p1) == true);
            REQUIRE(0 == M.taskQueueExecute() );

            THEN("The process exits")
            {
                REQUIRE(M.isRunning(p1) == false);

                THEN("Process does not exit gracefully. Executes trap")
                {
                    // The interrupt handled
                    REQUIRE(
                        O1->str() == "test_very_bad: enter loop\n"
                                     "test_very_bad: onExit\n"
                        );
                }
            }
        }
        WHEN("When we call destroy()")
        {
            REQUIRE(0 == M.destroy());

            THEN("Process exits")
            {
                REQUIRE(M.isRunning(p1) == false);

                THEN("P1 executes trap")
                {
                    // The interrupt handled
                    REQUIRE(
                        O1->str() == "test_very_bad: enter loop\n"
                                     "test_very_bad: onExit\n"
                        );
                }
            }
        }
    }
}
