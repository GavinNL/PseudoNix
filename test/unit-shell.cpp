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


std::pair<std::string, System::exit_code_type> testS(std::string script)
{
    System M;

    M.setFunction("sh", std::bind(shell_coro, std::placeholders::_1, ShellEnv{}));


    auto E = System::parseArguments({"sh"});
    // Here we're going to put our shell script code into the input
    // stream of the process function, similar to how linux works
    E.in  = System::make_stream(script);
    E.out = System::make_stream();
    E.in->set_eof();

    auto pid = M.runRawCommand(E);
    REQUIRE(pid == 1);
    auto exit_code = M.getProcessExitCode(pid);

    while(M.executeAll());

    auto str = E.out->str();
    while(str.back() == '\n')
        str.pop_back();
    return {str, *exit_code};
}


SCENARIO("Test Shell features")
{
    {
        auto [out, code] = testS("true && echo true;");
        REQUIRE(out == "true");
        REQUIRE(code == 0);
    }
    {
        auto [out, code] = testS("false && echo false;");
        REQUIRE(out == "");
        REQUIRE(code == 1);
    }
    {
        auto [out, code] = testS("false || echo false;");
        REQUIRE(out == "false");
        REQUIRE(code == 0);
    }
    {
        auto [out, code] = testS("true && echo true || echo false;");
        REQUIRE(out == "true");
        REQUIRE(code == 0);
    }
    {
        auto [out, code] = testS("false && echo true || echo false;");
        REQUIRE(out == "false");
        REQUIRE(code == 0);
    }
    {
        auto [out, code] = testS("echo hello $(echo world);");
        REQUIRE(out == "hello world");
        REQUIRE(code == 0);
    }
    {
        auto [out, code] = testS("echo hello | rev;");
        REQUIRE(out == "olleh");
        REQUIRE(code == 0);
    }
    {
        auto [out, code] = testS(
            "echo hello world;"
            "sleep 2 && echo sleep finished &;"
            "echo done;"
            "");
        REQUIRE(out ==
                "hello world\n"
                "3\n"
                "done\n"
                "sleep finished");
        REQUIRE(code == 0);
    }
}

