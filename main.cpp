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
echo "===================="
echo "Welcome to the shell"
echo "===================="
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




    // since we are running this application from the terminal, we need a way
    // to get the bytes from stdin and pipe it into the shell process.
    //
    // We're going to essentially run the following:
    //
    //     fromCin | sh | toCout
    //
    // We will create the two function fromCin and toCout below
    //
    M.m_funcs["fromCin"] = [](bl::MiniLinux::e_type control) -> bl::MiniLinux::task_type
    {
        auto & exev = *control;

        static auto count = 0;
        if(count != 0)
        {
            *exev.out << "Only one instance of fromCin can exist\n";
            co_return 1;
        }


        count++;
        while (!exev.is_sigkill())
        {
            // std::getline blocks until data is entered, but
            // we dont want to do that because this will block our entire
            // process
            // we want to check if bytes are available and then
            // read them in, if no bytes are there, we should suspend the
            // coroutine
            int bytes = 0;

            // check if there are any bytes in stdin
            if (ioctl(STDIN_FILENO, FIONREAD, &bytes) == -1) {
                co_return 1;
                //perror("ioctl");
            }

            if (bytes > 0)
            {
                std::string input;
                std::getline(std::cin, input);

                *exev.out << input;
                *exev.out << '\n';
            } else {
                co_await std::suspend_always{};
            }
        }
        count--;
        co_return 0;
    };


    M.m_funcs["toCout"] = [](bl::MiniLinux::e_type control) -> bl::MiniLinux::task_type
    {
        auto & exev = *control;
        while(!exev.is_sigkill())
        {
            while (exev.in->has_data() && !exev.is_sigkill())
            {
                std::string s;
                *exev.in >> s;
                std::cout << s;
            }
            co_await std::suspend_always{};
        }

        co_return 0;
    };


    // Finally lets start our first process
    // within the system. We will run the
    // the following:
    //
    //  fromCin | sh | toCout
    //
    //
    MiniLinux::Exec E[3];

    // Manually create each of the processes
    // and make sure to set the output of N to
    // the input of N+1
    E[0].args = {"fromCin"};
    E[0].in   = MiniLinux::make_stream();
    E[0].out  = MiniLinux::make_stream();

    E[1].args = {"sh"};
    E[1].in = E[0].out; // input to "sh" comes from fromCin
    E[1].out = MiniLinux::make_stream();
    E[1].env = {
        {"HOME", "/home/bob"} // we can set any env variable we want for this
    };

    E[2].args = {"toCout"};
    E[2].in = E[1].out;
    E[2].out = MiniLinux::make_stream(); // no need for output here

    // Throw all three commands onto
    // into the scheduler to run and
    // return their PIDs
    //
    // The functions are not executed yet
    // they are only executed when the scheduler
    // tells them to run.
    auto pid1 = M.runRawCommand(E[0]);
    auto pid2 = M.runRawCommand(E[1]);
    auto pid3 = M.runRawCommand(E[2]);

    // Run the scheduler so that it will
    // continuiously execute the coroutines
    //
    //
    while(M.executeAll())
    {
        // since we are actually running the "sh" function, we can technically
        // kill our own process using "ps" and "kill"
        //
        // We want to make sure that if any of the processes get killed
        // we kill all three of them.
        if(!M.isRunning(pid1) || !M.isRunning(pid2) || !M.isRunning(pid3))
        {
            M.kill(pid1);
            M.kill(pid2);
            M.kill(pid3);
        }
        // sleep for 1 millisecond so we're not
        // doing a busy loop
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return 0;
}


