#include <string>
#include <thread>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <ebash/MiniLinux.h>
#include <ebash/shell2.h>

bl::MiniLinux * _M = nullptr;
bl::MiniLinux::pid_type launcher_pid = 0xFFFFFFFF;

void handle_sigint(int signum)
{
    if(_M)
        _M->signal(launcher_pid, SIGINT);
};

int main()
{
    using namespace bl;


    // The first thing we need to do is create
    // the instance of the mini linux system
    //
    MiniLinux M;

    //=============================================================================
    // Add the shell function to MiniLinux
    // This isn't added by default because it's quite a large
    // function and you might want to add your own
    //
    //
    // This function also allows you to customize how the shell behaves
    // by binding an initial ShellEnv
    //
    bl::ShellEnv shellEnv;
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
    M.m_funcs["sh"] = std::bind(bl::shell2, std::placeholders::_1, shellEnv);
    //=============================================================================


    // If we start our main process, "sh", then it will create its own
    // input and output streams, but we have no way write to the input stream
    // or read from its output stream.
    //
    // This is normally fine because we'd likely want to use MiniLinux for
    // GUI applications, where we'd pipe the data from a textwidget into
    // the shell process.  But this is a command line example, so we want
    // to get the data from std::cin, and write to std::cout
    //
    // "launcher" is a process which will launch another process, but it will
    // read data from std::cin and copy it to the input stream of the child process
    // it will then read the output stream of the child process and output it to
    // std::cout
    //
    launcher_pid = M.runRawCommand(MiniLinux::parseArguments({"launcher2", "sh"}));

    // We are going to use a signal handler to
    // catch ctrl-C inputs and then
    // pass them into the launcher
    // process
    std::signal(SIGINT, handle_sigint);
    _M = &M;

    // executeAllFor( ) will keep calling executeAll()
    // until the total time elapsed is more than the
    // given input value
    while(M.executeAll())
    {
        // sleep for 1 millisecond so we're not
        // doing a busy loop
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return 0;
}


