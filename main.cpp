#include <string>
#include <thread>


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

int main()
{
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
        std::string input;
        uint32_t random_number = std::rand() % 100 + 1;
        *ctrl << std::format("I have chosen a number between 1-100. Can you guess what it is?\n");

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
                *ctrl->out << std::format("invalid entry: {}\n", line);
                *ctrl->out << std::format("Guess Again: \n");
                continue;
            }

            if(guess > random_number)
            {
                *ctrl->out << std::format("Too High!\n");
            }
            else if(guess < random_number)
            {
                *ctrl->out << std::format("Too Low!\n");
            }
            else
            {
                *ctrl->out << std::format("Awesome! You guessed the correct number: {}!\n", random_number);
                *ctrl->out << std::format("Exiting\n");
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
    launcher_pid = M.runRawCommand(System::parseArguments({"launcher", "sh"}));

    // We are going to use a signal handler to
    // catch ctrl-C inputs and then
    // pass them into the launcher
    // process
    std::signal(SIGINT, handle_sigint);
    _M = &M;

    // executeAllFor( ) will keep calling executeAll()
    // until the total time elapsed is more than the
    // given input value
    while(M.executeAllFor(std::chrono::milliseconds(1), 10))
    {
        // sleep for 1 millisecond so we're not
        // doing a busy loop
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    _M = nullptr;
    return 0;
}


