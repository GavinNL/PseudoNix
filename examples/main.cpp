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
    if(_M)
        _M->signal(launcher_pid, PseudoNix::sig_interrupt);
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
    auto p1 = M.spawnProcess({"bgrunner", "THREADPOOL"});
    auto p2 = M.spawnProcess({"bgrunner", "THREADPOOL"});

    // Since bgrunner is spawned without a shell
    // there is no way for it to exit by itself
    // so when the shell process exits, the
    // while loop will still continue
    // So to fix this, we'll run one instance of the
    // taskQueueExecute to see the total number of tasks
    // there will be without user interaction
    auto min_tasks   =  M.taskQueueExecute("PRE_MAIN") +
                       + M.taskQueueExecute("MAIN")
                       + M.taskQueueExecute("POST_MAIN");


    // This while loop is basically your system's gameloop
    // You should call taskQueueExecute("MAIN")
    // in somwhere
    //
    while(true)
    {       
        auto total_tasks =  M.taskQueueExecute("PRE_MAIN") +
                          + M.taskQueueExecute("MAIN")
                          + M.taskQueueExecute("POST_MAIN");

        if(total_tasks == 0)
            break;

        // Then if the total tasks remaining is less than
        // the min_tasks (ie: sh exited), we can signal the two bgrunners
        // to exit
        if(total_tasks < min_tasks)
        {
            std::cerr << "Signaling bgrunners to exit" << std::endl;
            M.signal(p1, PseudoNix::sig_interrupt);
            M.signal(p2, PseudoNix::sig_interrupt);
        }
        // sleep for 1 millisecond so we're not
        // doing a busy loop
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    _M = nullptr;
    return 0;
}
