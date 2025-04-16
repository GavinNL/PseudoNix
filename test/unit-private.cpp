#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <PseudoNix/System.h>
#include <PseudoNix/Shell.h>

using namespace PseudoNix;


SCENARIO("Tokenizer 3")
{
    {
        auto v = Tokenizer3::to_vector("\\$\\(sleep");
        REQUIRE(v[0] == "$(sleep");
    }

    {
        auto v = Tokenizer3::to_vector("echo hello $(sleep 3 && echo world)");
        REQUIRE(v[0] == "echo");
        REQUIRE(v[1] == "hello");
        REQUIRE(v[2] == "$(sleep 3 && echo world)");
    }
    {
        auto v = Tokenizer3::to_vector("echo $(get word) $(sleep $(get count) && echo world)");
        REQUIRE(v[0] == "echo");
        REQUIRE(v[1] == "$(get word)");
        REQUIRE(v[2] == "$(sleep $(get count) && echo world)");
    }

    {
        auto v = Tokenizer3::to_vector("echo 1 && echo 2 || echo 3");
        REQUIRE(v[0] == "echo");
        REQUIRE(v[1] == "1");
        REQUIRE(v[2] == "&&");
        REQUIRE(v[3] == "echo");
        REQUIRE(v[4] == "2");
        REQUIRE(v[5] == "||");
        REQUIRE(v[6] == "echo");
        REQUIRE(v[7] == "3");
    }
}


SCENARIO("System: sh")
{
    System M;
    M.setFunction("sh", std::bind(shell_coro, std::placeholders::_1, ShellEnv{}));

    System::Exec sh;
    sh.args = {"sh"};
    sh.in  = std::make_shared<System::stream_type>();
    sh.out = std::make_shared<System::stream_type>();

    *sh.in << "echo hello world;";
    sh.in->set_eof();
    M.runRawCommand(sh);

    while(M.executeAll());

    REQUIRE(sh.out->str() == "hello world");
}



SCENARIO("System: sh - multicommand")
{
    System M;
    M.setFunction("sh", std::bind(shell_coro, std::placeholders::_1, ShellEnv{}));

    System::Exec sh;
    sh.args = {"sh"};
    sh.in  = std::make_shared<System::stream_type>();
    sh.out = std::make_shared<System::stream_type>();

    *sh.in << "echo hello ; sleep 1.0 ; echo world;";
    sh.in->set_eof();

    REQUIRE(0xFFFFFFFF != M.runRawCommand(sh) );
    while(M.executeAll());

    REQUIRE(sh.out->str() == "helloworld");
}



SCENARIO("System: sh - pipe")
{
    System M;
    M.setFunction("sh", std::bind(shell_coro, std::placeholders::_1, ShellEnv{}));
    THEN("We can wr")
    {
        System::Exec sh;
        sh.args = {"sh"};
        sh.in  = std::make_shared<System::stream_type>();
        sh.out = std::make_shared<System::stream_type>();
        *sh.in << "echo hello | wc && echo goodbye;";
        sh.in->set_eof();

        REQUIRE(0xFFFFFFFF != M.runRawCommand(sh) );
        while(M.executeAll());

        REQUIRE(sh.out->str() == "5\ngoodbye");
        //sh.out->toStream(std::cout);
        //std::cout << std::endl;
    }
}


