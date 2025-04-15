#ifndef PSUDONIX_LAUNCHER_H
#define PSUDONIX_LAUNCHER_H

#include "MiniLinux.h"
#include "defer.h"

namespace PseudoNix
{

inline System::task_type launcher_coro(System::e_type ctrl)
{
    static auto count = 0;
    if(count != 0)
    {
        *ctrl << std::format("Only one instance of {} can exist\n", ctrl->args[0]);
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
    auto [c_in, c_out] = ctrl->mini->getIO(sh_pid);
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

        // We technically dont need to do this because
        // the output stream is automatically closed
        // by System when launcher completes
        // either by forcefully removing it, or
        // if it completes successfully. But just in case
        // we should shutdown c_in because it is passed
        // into the subprocess as a input stream
        c_in->close();
        c_out->close();
    };


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
               // std::cerr << "EOF found" << std::endl;
            }
            return result;
            //return result == 1;
        }
        return ssize_t{-1};
        //return false;
    };

    while(true)
    {
        char ch=0;
        while(true)
        {
            auto result = read_char_nonblocking(&ch);
            if(result == 1)
                E.in->put(ch);
            else if(result == 0)
            {
                ctrl->mini->signal(sh_pid, 900);
                std::cerr << "EOF" << std::endl;
            }
            if(result==-1)
                break;
        }

        // If there are any bytes in the output stream of
        // sh, read them and write them to std::cout
        while(c_out->has_data())
            std::cout.put( c_out->get());

        // Check if the sh function is still running
        // if not, quit.
        if(ctrl->areSubProcessesFinished())
        {
            break;
        }

        // Suspend this coroutine here but quit
        // if we recieve a SIGTERM signal
        //
        // If we were creating a process that didn't
        // spawn a subprocess, we'd use SUSPEND_SIG_INT_TERM(ctrl)
        // The reason we are not doing that is because
        // SUSPEND_SIG_INT_TERM(ctrl) will exit the
        // coroutine if we recieved a SIGINT (interrupt)
        // We dont want to do this because SIGINT will be
        // passed into the subprocess by default
        SUSPEND_SIG_TERM(ctrl)
    }

    count--;

    std::cout << std::format("{} exiting", ctrl->args[0]) << std::endl;
    co_return 0;
}


}

#endif
