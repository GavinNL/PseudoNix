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

    MiniLinux M;

    M.m_funcs["sh"] = bl::shell2;

    // since we are running this application from the terminal, we need a way
    // to get the bytes from stdin and pipe it into the shell process.
    //
    // We're going to essentially run this following:
    //
    //     fromCin | sh | toCout
    //
    // We will create the two function fromCin and toCout below
    //
    M.m_funcs["fromCin"] = [](bl::MiniLinux::Exec exev) -> bl::MiniLinux::task_type
    {
        while (!exev.is_sigkill())
        {
            // std::getline blocks until data is entered, but
            // we dont want to do that because this will block our entire
            // process
            // we want to check if bytes are available and then
            // read them in, if no bytes are there, we should suspend the
            // coroutine
            int bytes = bytes_available_in_stdin();

            if (bytes > 0) {
                std::string input;
                std::getline(std::cin, input);

                *exev.out << input;
                *exev.out << '\n';
            } else {
                co_await std::suspend_always{};
            }
        }
        co_return 0;
    };

    M.m_funcs["toCout"] = [](bl::MiniLinux::Exec exev) -> bl::MiniLinux::task_type
    {
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

    MiniLinux::Exec E[3];

    // Manually create each of the processes
    // and make sure to set the output of N to
    // the input of N+1
    E[0].args = {"fromCin"};
    E[0].in = MiniLinux::make_stream(); // no need for input here
    E[0].out = MiniLinux::make_stream();

    E[1].args = {"sh"};
    E[1].in = E[0].out;
    E[1].out = MiniLinux::make_stream();

    E[2].args = {"toCout"};
    E[2].in = E[1].out;
    E[2].out = MiniLinux::make_stream(); // no need for output here

    // Throw all three commands onto
    // into the scheduler to run
    auto pid1 = M.runRawCommand2(E[0]);
    auto pid2 = M.runRawCommand2(E[1]);
    auto pid3 = M.runRawCommand2(E[2]);

    // Run the scheduler so that it will
    // continuiously execute the coroutines

    while(M.executeAll())
    {
        // since we are actually running the "sh" function, we can technically
        // kill our own process using "ps" and "kill"
        //
        // We want to make sure that if any of the processes get killed
        // we kill all three of them. Otherwise, killing one of them
        // will halt the program since they are not receiving data

        if(!M.isRunning(pid1) || !M.isRunning(pid2) || !M.isRunning(pid3))
        {
            std::cout << "Killing all" << std::endl;
            M.kill(pid1);
            M.kill(pid2);
            M.kill(pid3);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::cout << "Exit" << std::endl;

    return 0;
}


