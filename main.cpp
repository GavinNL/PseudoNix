#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <memory>
#include <chrono>
#include <thread>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <ebash/MiniLinux.h>
#include <ebash/SimpleScheduler.h>
#include <ebash/shell.h>

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

gul::Task_t<int> read_from_cin(std::shared_ptr<bl::MiniLinux::stream_type> _out)
{
    while (true) {
        int bytes = bytes_available_in_stdin();
        if (bytes > 0) {
            //std::cout << "Bytes available in stdin: " << bytes << '\n';
            std::string input;
            std::getline(std::cin, input);
            //std::cout << "Read input: " << input << '\n';
            *_out << input;
            *_out << '\n';
        } else {
            //std::cout << "No input yet, waiting...\n";
            co_await std::suspend_always{};
        }
    }
}

int main()
{
    using namespace bl;

    MiniLinux M;
    SimpleScheduler S;


    M.m_scheduler = std::bind(&bl::SimpleScheduler::emplace_process, &S, std::placeholders::_1, std::placeholders::_2);

    M.funcs["sh"] = bl::shell;

    MiniLinux::Exec E;
    E.args = {"sh"};

    // Here we're going to put our shell script code into the input
    // stream of the process function, similar to how linux works
    E.in  = MiniLinux::make_stream();
    E.out = {}; // blank means it will go to std::cout

    // finally get the coroutine task and place it
    // into our scheduler
    //auto shell_task = M.runRawCommand(E);

    M.system(E);

    //S.emplace(std::move(shell_task));
    S.emplace(read_from_cin(E.in));
    // Run the scheduler so that it will
    // continuiously execute the coroutines
    while(S.run_once())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        // we have our special task that is used to read
        // from stdin and put it into the stream
        // and one "sh" task, if the shell task is closed
        // then we can exit the program
        if(S._tasks.size() == 1)
        {
            break;
        }
    }


    return 0;
}

