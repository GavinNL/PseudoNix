#include <string>
#include <thread>

#include <PseudoNix/System.h>


PseudoNix::System::task_type mycustomfunction(PseudoNix::System::e_type ctrl)
{
    auto sleep_time = std::chrono::milliseconds(250);
    for(int i=0;i<10;i++)
    {
        std::cout << std::format("[{}] Counter: {}", ctrl->args[1], i) << std::endl;

        // yield some time back to the scheduler
        // so that other processes can execute
        (void)co_await ctrl->await_yield_for(sleep_time);
    }
    co_return 0;
}


int main(int argc, char ** argv)
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

    // run 3 instances of the coroutine using different input
    // arguments
    M.spawnProcess({"mycustomfunction", "alice"});
    M.spawnProcess({"mycustomfunction", "bob"});
    M.spawnProcess({"mycustomfunction", "charlie"});


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


