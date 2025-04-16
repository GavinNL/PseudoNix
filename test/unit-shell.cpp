#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <PseudoNix/System.h>
#include <PseudoNix/Shell.h>

using namespace PseudoNix;


#if 1
SCENARIO("test shell")
{
    System M;

    M.m_funcs["sh"] = std::bind(shell_coro, std::placeholders::_1, ShellEnv{});


    System::Exec E;
    E.args = {"sh"};

    // Here we're going to put our shell script code into the input
    // stream of the process function, similar to how linux works
    E.in  = System::make_stream(R"foo(echo hello world
echo hello world | rev
echo hello again
exit
)foo");
    // We're going to close the stream so that the task will complete, otherwise
    // it will keep trying to read code.
    E.in->close();
    E.out = System::make_stream();

    // Finally execute the command and get the PID
    // The command does not do any processing until execute() is called
    auto pid = M.runRawCommand(E);
    REQUIRE(pid == 1);

    // run an infinate loop
    // processing all the coroutines
    while(M.executeAll());

    std::string out;
    *E.out >> out;
    std::cout << out << std::endl;
    REQUIRE(out ==
R"foo(hello world
dlrow olleh

hello again
exit)foo");

    exit(0);
}


#endif
