#include <string>
#include <thread>

//#define PSUEDONIX_ENABLE_DEBUG
#include <PseudoNix/System.h>
#include <PseudoNix/Shell.h>
#include <PseudoNix/Launcher.h>

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

    //=============================================================================
    // Add the shell function to System
    // This isn't added by default because it's quite a large
    // function and you might want to add your own
    //
    //
    // This function also allows you to customize how the shell behaves
    // by binding an initial ShellEnv
    //
    PseudoNix::ShellEnv shellEnv;
    shellEnv.rc_text = R"foo(
echo "========================================================"
echo "Welcome to the shell"
echo " "
echo "use the [help] command to see all the commands available
echo "========================================================"
SHELL=sh
USER=bob
echo Hello ${USER}. Welcome to ${SHELL}
export USER
export SHELL
export HOME

)foo";
    // bind the shellEnv input to the shell function
    M.setFunction("sh", std::bind(PseudoNix::shell_coro, std::placeholders::_1, shellEnv));

    M.setFunction("launcher", PseudoNix::launcher_coro);
    //=============================================================================

    M.setFunction("guess", [](PseudoNix::System::e_type ctrl) -> PseudoNix::System::task_type
    {
        // Macro to define a few variables such as
        // IN, OUT, ENV, SYSTEM, ARGS, PID
        PSEUDONIX_PROC_START(ctrl);

        std::string input;
        uint32_t random_number = std::rand() % 100 + 1;
        COUT << std::format("I have chosen a number between 1-100. Can you guess what it is?\n");

        while(true)
        {
            std::string line;

            // HANDLE_AWAIT_INT_TERM is a macro that looks at the return type of the
            // Awaiter (a signal code), and co_returns the appropriate
            // exit code. It will exit if the code is SIG_TERM or SIG_INT
            //
            // This is where Ctrl-C and Sig-kills are handled
            HANDLE_AWAIT_INT_TERM(co_await ctrl->await_read_line(ctrl->in, line), ctrl)

            uint32_t guess = 0;

            if(std::errc() != std::from_chars(line.data(), line.data() + line.size(), guess).ec)
            {
                COUT << std::format("invalid entry: {}\n", line);
                COUT << std::format("Guess Again: \n");
                continue;
            }

            if(guess > random_number)
            {
                COUT << std::format("Too High!\n");
            }
            else if(guess < random_number)
            {
                COUT  << std::format("Too Low!\n");
            }
            else
            {
                COUT << std::format("Awesome! You guessed the correct number: {}!\n", random_number);
                COUT << std::format("Exiting\n");
                co_return 0;
            }
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

    // Create another task queue
    // This is for the taskHopper example
    //
    // In the future, taskQueues will be able to be executed on
    // different threads, so you can structure your processes
    // with background computation as well
    M.taskQueueCreate("PRE_MAIN");
    M.taskQueueCreate("POST_MAIN");
    M.taskQueueCreate("THREADPOOL");

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
