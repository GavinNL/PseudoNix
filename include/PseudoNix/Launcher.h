#ifndef PSUDONIX_LAUNCHER_H
#define PSUDONIX_LAUNCHER_H

#include "System.h"
#include "defer.h"

#if defined(_WIN32)
#include <windows.h>
#include <conio.h>
#include <cstddef>
#else
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#endif

namespace PseudoNix
{

inline System::task_type launcher_coro(System::e_type ctrl)
{
    static auto count = 0;
    if(count != 0)
    {
        *ctrl->in << std::format("Only one instance of {} can exist\n", ctrl->args[0]);
        co_return 1;
    }

    count++;

    if(ctrl->args.size() < 2)
    {
        std::cout << "Requires a command to be called\n\n";
        std::cout << std::format("   {} sh\n", ctrl->args[0]);
        co_return 1;
    }

    auto E = System::parseArguments(std::vector(ctrl->args.begin()+1, ctrl->args.end()));
    // Instead of using a default provided
    // input stream for the subprocess
    // we'll use launcher's input stream
    // since it is not connected to anything
    E.in = ctrl->in;
    E.out = ctrl->out;

    // Execute the sub process and get the
    // PID. Executing as a subprocess
    // will allow passthrough of signals
    // to the sub process
    auto sh_pid = ctrl->executeSubProcess(E);

    if(sh_pid == 0xFFFFFFFF)
    {
        std::cout << "Invalid Command: " << ctrl->args[1] << std::endl;
        co_return 127;
    }

    // Get the input and output streams for the
    // shell process
    auto [c_in, c_out] = ctrl->system->getIO(sh_pid);
    assert(c_in == E.in);
    assert(c_out == E.out);

    bl_defer
    {
        // count was decremented at the
        // end of the function, but we might not actually
        // get there if this process was forcefully killed
        // so to ensure it gets decremnted properly
        // we'll put it in a defer block
        if(count) count--;
    };




#if defined(_WIN32)
    static const auto read_char_nonblocking = [](char* ch) -> int64_t {
        if (_kbhit()) {
            int c = _getch(); // returns int to handle special keys
            if (c == 0 || c == 224) {
                // Handle extended keys (e.g., arrows), discard second byte
                _getch();
                return -1; // You can return something custom here if desired
            }
            if (c == '\r')
            {
                c = '\n';
            }
            std::cout.put(c);
            *ch = static_cast<char>(c);
            
            return 1; // Successfully read one character
        }
        return -1; // No input available
    };
#else
    static const auto read_char_nonblocking = [](char *ch) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);

        struct timeval timeout = {0,0}; // non-blocking
        int ready = select(STDIN_FILENO + 1, &fds, NULL, NULL, &timeout);

        if (ready > 0 && FD_ISSET(STDIN_FILENO, &fds)) {
            ssize_t result = read(STDIN_FILENO, ch, 1);
            if(result == 0)
            {
               //std::cerr << "EOF found" << std::endl;
            }
            return result;
            //return result == 1;
        }
        return ssize_t{-1};
        //return false;
    };
#endif

    std::cerr << std::format("Launcher started process: {}", ctrl->args[1]) << std::endl;
    while(true)
    {
        char ch=0;
        while(true)
        {
            auto result = read_char_nonblocking(&ch);
            if(result==-1)
            {
                //std::cerr << "no data" << std::endl;
                break;
            }
            else if(result == 1)
            {
                E.in->put(ch);
            }
            else if(result == 0)
            {
                //std::cerr << "EOF" << std::endl;
                // c_in->set_eof();
            }
        }

        // If there are any bytes in the output stream of
        // sh, read them and write them to std::cout
        char c;
        while(c_out->get(&c) == ReaderWriterStream::Result::SUCCESS)
        {
            std::cout.put(c);
        }
        std::cout << std::flush;

        // Check if the sh function is still running
        // if not, quit.
        if(!ctrl->system->isRunning(sh_pid))
        {
            break;
        }

        HANDLE_AWAIT_TERM(co_await ctrl->await_yield(), ctrl)
    }

    count--;

    std::cerr << std::format("{} exiting", ctrl->args[0]) << std::endl;
    co_return 0;
}


}

#endif
