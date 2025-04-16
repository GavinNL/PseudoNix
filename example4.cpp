#include <string>
#include <thread>

#include <PseudoNix/System.h>
#include <PseudoNix/Launcher.h>
#include <PseudoNix/Shell.h>

PseudoNix::System::task_type mycustomfunction(PseudoNix::System::e_type ctrl)
{
    PSEUDONIX_PROC_START(ctrl);
    auto sleep_time = std::chrono::milliseconds(250);

    PSEUDONIX_TRAP {
        // This will be called even if you call "kill"
        // on the pid
        OUT << std::format("This is executed on cleanup.");
    };

    int i=0;
    while(true)
    {
        OUT << std::format("Counter: {}\n", i++);

        // await for the awaiter to signal
        // if it does, break the while loop if
        // it returned any of the known signals:
        //  sig_terminate, sig_interrupt
        HANDLE_AWAIT_BREAK_ON_SIGNAL(co_await ctrl->await_yield_for(sleep_time), ctrl);
    }

    // this will only be called if the while loop exits
    // properly by reacting to a signal, either:
    // signal PID 2
    // signal PID 15
    OUT << std::format("This a graceful exit\n");
    co_return 0;
}


int main()
{
    using namespace PseudoNix;

    // The first thing we need to do is create
    // the instance of the mini linux system
    //
    System M;

    // add our coroutine to the list of functions to be
    // called
    M.setFunction("sh", std::bind(PseudoNix::shell_coro, std::placeholders::_1, ShellEnv{}));
    M.setFunction("launcher", PseudoNix::launcher_coro);

    M.setFunction("mycustomfunction", mycustomfunction);

    // We can manually create a pipeline. This will
    // pipe the output of one ffunction into the input of another
    // just like in linux:  mycustomfunction | to_std_cout
    //
    // The to_std_cout function is provided for you
    // It simply takes whatever is in its input buffer
    // and writes it to std::cout
    M.spawnPipelineProcess({
            {"launcher", "sh"}
    });

    // executeAllFor( ) will keep calling executeAll()
    // until the total time elapsed is more than the
    // given input value
    while(M.executeAllFor(std::chrono::milliseconds(1), 10))
    {
        // sleep for 1 millisecond so we're not
        // doing a busy loop
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return 0;
}


