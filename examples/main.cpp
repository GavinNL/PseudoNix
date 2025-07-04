#include <string>
#include <thread>

//#define PSUEDONIX_ENABLE_DEBUG
#include <PseudoNix/Launcher.h>
#include "common_setup.h"

#include <csignal>

PseudoNix::System * _M = nullptr;
PseudoNix::System::pid_type launcher_pid = 0xFFFFFFFF;

void handle_sigint(int signum)
{
    (void)signum;
    if(_M)
        _M->signal(launcher_pid, PseudoNix::eSignal::INTERRUPT);
};

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    using namespace PseudoNix;

    // The first thing we need to do is create
    // the instance of the mini linux system
    //
    System M;

    setup_functions(M);
    //=============================================================================
    // Add the shell function to System
    // This isn't added by default because it's quite a large
    // function and you might want to add your own
    M.setFunction("launcher", "Launches another process and redirects stdin/out to the process.", PseudoNix::launcher_coro);
    //=============================================================================

    M.setFunction("bad", "A bad function that does not listen to signals", [](PseudoNix::System::e_type ctrl) -> PseudoNix::System::task_type
    {
        // Macro to define a few variables such as
        // IN, OUT, ENV, SYSTEM, ARGS, PID
        PN_PROC_START(ctrl);

        PSEUDONIX_TRAP {
            std::cerr << "BAD TRAPPED" << std::endl;
        };
        COUT << std::format("BAD process started. Awaiting, but not listening to signals\n");

        while(true)
        {
            // await, but dont do anything with the
            // return code.
            auto y = co_await ctrl->await_yield();
            (void)y;

            auto x = co_await ctrl->await_yield_for(std::chrono::milliseconds(75));
            (void)x;
        }

        co_return 0;
    });

    // If we start our main process, "sh", then it will create its own
    // input and output streams, but we have no way write to the input stream
    // or read from its output stream.
    //
    // This is normally fine because we'd likely want to use System for
    // GUI applications, where we'd pipe the data from a textwidget into
    // the shell process.  But this is a command line example, so we want
    // to get the data from std::cin, and write to std::cout
    //
    // "launcher" is a process which will launch another process, but it will
    // read data from std::cin and copy it to the input stream of the child process
    // it will then read the output stream of the child process and output it to
    // std::cout
    //
    launcher_pid = M.spawnProcess({"launcher", "sh"});

    // We are going to use a signal handler to
    // catch ctrl-C inputs and then
    // pass them into the launcher
    // process
    std::signal(SIGINT, handle_sigint);
    _M = &M;

    // Spawn 2 background runners to process the
    // THREADPOOL queue.
    //
    // Note:
    M.spawnProcess({"bgrunner", "THREADPOOL"});
    M.spawnProcess({"bgrunner", "THREADPOOL"});


    // This while loop is basically your system's gameloop
    // You should call taskQueueExecute("MAIN")
    // in somwhere
    //
    while(true)
    {
        M.taskQueueExecute("PRE_MAIN");
        M.taskQueueExecute("MAIN");
        M.taskQueueExecute("POST_MAIN");

        if(M.process_count() == 0)
            break;

        if(!M.isRunning(launcher_pid))
        {
            // Since bgrunner is spawned without a shell
            // there is no way for it to exit by itself
            // so when the shell process exits, the
            // bgrunners will still continue running.
            //
            // So to fix this, we'll check that launcher
            // process is still running, since we will only
            // have one of these running at a time, we can use
            // this to indicate to exit the program
            //
            // We will use the terminateAll() to
            // terminate all processes by sending it
            // a sig_term signal.
            //
            // NOTE: The processes must be written to await
            //       correctly, otherwise this will not work
            //
            // NOTE 2: terminateAll() will ask the process to
            //         gracefully terminate itself.
            //         Its possible that the process does not
            //         listen to the signal
            M.terminateAll();
            break;
        }
        // sleep for 1 millisecond so we're not
        // doing a busy loop
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Clean up the system by doing the following steps:
    //
    // 1. Send a SIG_TERM to all running processes to
    //    gracefully exit
    // 2. Wait for them to exit
    // 3. Send a kill signal to the remaining processes
    // 4. Wait for them to exit
    // 5. Forcefully remove all left over processes
    M.destroy();

    _M = nullptr;
    return 0;
}
