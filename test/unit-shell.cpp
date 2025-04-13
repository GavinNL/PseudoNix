#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <ebash/shell2.h>

using namespace PseudoNix;


SCENARIO("TEST tokenize")
{
    using Tokenizer = Tokenizer2;

    {
        auto args = Tokenizer::to_vector("XX=\"hello world\" YY=fdasdf");

        REQUIRE(args[0] == "XX=hello world");
        REQUIRE(args[1] == "YY=fdasdf");
    }

    {
        auto args = Tokenizer::to_vector("echo hello|rev");

        REQUIRE(args[0] == "echo");
        REQUIRE(args[1] == "hello");
        REQUIRE(args[2] == "|");
        REQUIRE(args[3] == "rev");
    }

    {
        auto args = Tokenizer::to_vector("echo hello | rev");

        REQUIRE(args[0] == "echo");
        REQUIRE(args[1] == "hello");
        REQUIRE(args[2] == "|");
        REQUIRE(args[3] == "rev");
    }

    {
        auto args = Tokenizer::to_vector("echo hello && rev");

        REQUIRE(args[0] == "echo");
        REQUIRE(args[1] == "hello");
        REQUIRE(args[2] == "&&");
        REQUIRE(args[3] == "rev");
    }
    {
        auto args = Tokenizer::to_vector("echo hello&&rev");

        REQUIRE(args[0] == "echo");
        REQUIRE(args[1] == "hello");
        REQUIRE(args[2] == "&&");
        REQUIRE(args[3] == "rev");
    }
    {
        auto args = Tokenizer::to_vector("echo hello||rev");

        REQUIRE(args[0] == "echo");
        REQUIRE(args[1] == "hello");
        REQUIRE(args[2] == "||");
        REQUIRE(args[3] == "rev");
    }

    {
        auto args = Tokenizer::to_vector("echo hello $(echo gavin&&echo world) && echo second run");

        REQUIRE(args[0] == "echo");
        REQUIRE(args[1] == "hello");
        REQUIRE(args[2] == "$(");
        REQUIRE(args[3] == "echo");
        REQUIRE(args[4] == "gavin");
        REQUIRE(args[5] == "&&");
        REQUIRE(args[6] == "echo");
        REQUIRE(args[7] == "world");
        REQUIRE(args[8] == ")");
        REQUIRE(args[9] == "&&");
        REQUIRE(args[10] == "echo");
        REQUIRE(args[11] == "second");
        REQUIRE(args[12] == "run");
    }

    {
        auto args = Tokenizer::to_vector("echo hello||rev &");

        REQUIRE(args[0] == "echo");
        REQUIRE(args[1] == "hello");
        REQUIRE(args[2] == "||");
        REQUIRE(args[3] == "rev");
        REQUIRE(args[4] == "&");
    }
}

#if 1
SCENARIO("test shell")
{
    System M;

    M.m_funcs["sh"] = std::bind(shell2, std::placeholders::_1, ShellEnv{});


    System::Exec E;
    E.args = {"sh"};

    // Here we're going to put our shell script code into the input
    // stream of the process function, similar to how linux works
    E.in  = System::make_stream(R"foo(
echo hello world
sleep 2
echo hello world | rev
ps
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

    //E.out->toStream(std::cout);
    exit(0);
}


#endif
