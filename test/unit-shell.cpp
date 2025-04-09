#include <catch2/catch_all.hpp>
#include <catch2/benchmark/catch_benchmark_all.hpp>
#include <ebash/shell2.h>

using namespace bl;


SCENARIO("TEST tokenize")
{
    using Tokenizer = Tokenizer2;

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
}


SCENARIO("test shell")
{
    MiniLinux M;

    M.m_funcs["sh"] = bl::shell2;


    MiniLinux::Exec E;
    E.args = {"sh"};

    // Here we're going to put our shell script code into the input
    // stream of the process function, similar to how linux works
    E.in  = MiniLinux::make_stream(R"foo(
echo hello world
sleep 2
echo goodbye world | rev
sleep 1
echo hello again
)foo");
    // We're going to close the stream so that the task will complete, otherwise
    // it will keep trying to read code.
    E.in->close();

    // Finally execute the command and get the PID
    // The command does not do any processing until execute() is called
    auto pid = M.runRawCommand2(E);
    REQUIRE(pid == 1);

    // run an infinate loop
    // processing all the coroutines
    while(M.executeAll());

    //E.out->toStream(std::cout);
    exit(0);
}


