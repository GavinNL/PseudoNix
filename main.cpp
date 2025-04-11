#include <iostream>
#include <string>
#include <memory>
#include <chrono>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <ebash/MiniLinux.h>
#include <ebash/shell2.h>

bool is_cin_ready() {
    // Get the current flags of stdin
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    // Set stdin to non-blocking
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    char c;
    bool ready = (read(STDIN_FILENO, &c, 1) > 0);

    // Restore original flags
    fcntl(STDIN_FILENO, F_SETFL, flags);

    if (ready) {
        // Push the character back into the stream
        ungetc(c, stdin);
    }

    return ready;
}

int bytes_available_in_stdin() {
    int count = 0;
    // Query stdin (file descriptor 0)
    if (ioctl(STDIN_FILENO, FIONREAD, &count) == -1) {
        perror("ioctl");
        return -1;
    }
    return count;
}

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
    auto pids = M.runRawCommand(MiniLinux::parseArguments({"launcher", "sh"}));

    while(M.executeAll())
    {
        // sleep for 1 millisecond so we're not
        // doing a busy loop
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return 0;
}


