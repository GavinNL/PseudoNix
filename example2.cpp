#include <string>
#include <thread>

#include <PseudoNix/MiniLinux.h>


PseudoNix::System::task_type my_custom_function(PseudoNix::System::e_type ctrl)
{
    auto sleep_time = std::chrono::milliseconds(250);
    for(int i=0;i<10;i++)
    {
        // Unlike in Example1, where we wrote directly
        // to std::cout, we are going to write
        // to the Output Stream of the process
        //
        // The output stream is not connected to anything
        *ctrl << std::format("[{}] Counter: {}\n", ctrl->args[1], i);

        co_await ctrl->await_yield_for(sleep_time);
    }
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
    M.setFunction("mycustomfunction", my_custom_function);

    // We can manually create a pipeline. This will
    // pipe the output of one ffunction into the input of another
    // just like in linux:  mycustomfunction | to_std_cout
    //
    // The to_std_cout function is provided for you
    // It simply takes whatever is in its input buffer
    // and writes it to std::cout
    M.runPipeline(M.genPipeline({
            {"mycustomfunction", "alice"},
            {"to_std_cout"}
    }));
    M.runPipeline(M.genPipeline({
        {"mycustomfunction", "bob"},
        {"to_std_cout"}
    }));
    M.runPipeline(M.genPipeline({
        {"mycustomfunction", "charlie"},
        {"to_std_cout"}
    }));

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


