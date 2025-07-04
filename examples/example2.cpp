#include <string>
#include <thread>

#include <PseudoNix/System.h>


PseudoNix::System::task_type mycustomfunction(PseudoNix::System::e_type ctrl)
{
    // Helper function to define a few
    // easy to use variables
    // such as IN and COUT streams
    PN_PROC_START(ctrl);

    auto sleep_time = std::chrono::milliseconds(250);
    for(int i=0;i<10;i++)
    {
        // Unlike in Example1, where we wrote directly
        // to std::cout, we are going to write
        // to the Output Stream of the process
        //
        // The output stream is not connected to anything
        COUT << std::format("[{}] Counter: {}\n", ARGS[1], i);

        (void)co_await ctrl->await_yield_for(sleep_time);
    }
    co_return 0;
}


int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    using namespace PseudoNix;

    // The first thing we need to do is create
    // the instance of the mini linux system
    //
    System M;

    // add our coroutine to the list of functions to be
    // called
    M.setFunction("mycustomfunction", mycustomfunction);

    // We can manually create a pipeline. This will
    // pipe the output of one ffunction into the input of another
    // just like in linux:  mycustomfunction | to_std_cout
    //
    // The to_std_cout function is provided for you
    // It simply takes whatever is in its input buffer
    // and writes it to std::cout
    M.spawnPipelineProcess({
            {"mycustomfunction", "alice"},
            {"to_std_cout"}
    });
    M.spawnPipelineProcess({
        {"mycustomfunction", "bob"},
        {"to_std_cout"}
    });
    M.spawnPipelineProcess({
        {"mycustomfunction", "charlie"},
        {"to_std_cout"}
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


