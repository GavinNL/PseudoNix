#include <string>
#include <thread>

#include <PseudoNix/System.h>
#include <PseudoNix/Launcher.h>
#include <PseudoNix/Shell.h>


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
    M.setFunction("sh", PseudoNix::shell_coro);
    M.setFunction("launcher", PseudoNix::launcher_coro);

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


