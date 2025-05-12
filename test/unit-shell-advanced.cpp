#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

//#define PSEUDONIX_LOG_LEVEL_SYSTEM
//#define PSEUDONIX_LOG_LEVEL_TRACE
//#define PSEUDONIX_LOG_LEVEL_INFO

#include <PseudoNix/System.h>
#include <PseudoNix/Shell1.h>

using namespace PseudoNix;


std::pair<std::string, System::exit_code_type> testS1(std::string script)
{
    System M;

    M.taskQueueCreate("PRE_MAIN");
    M.setFunction("sh", PseudoNix::shell_coro);


    auto E = System::parseArguments({"sh"});
    // Here we're going to put our shell script code into the input
    // stream of the process function, similar to how linux works
    E.in  = System::make_stream(script);
    E.out = System::make_stream();
    E.in->set_eof();

    auto pid = M.runRawCommand(E);
    REQUIRE(pid == 1);
    auto exit_code = M.getProcessExitCode(pid);


    while(M.taskQueueExecute("PRE_MAIN") + M.taskQueueExecute("MAIN"));

    auto str = E.out->str();
    while(str.size() && str.back() == '\n')
        str.pop_back();
    return {str, *exit_code};
}

std::pair<std::string, System::exit_code_type> testS(std::string script)
{
    System M;

    M.touch("/script.sh");
    M.fs("/script.sh") << script;

    M.taskQueueCreate("PRE_MAIN");
    M.setFunction("sh", PseudoNix::shell_coro);


    auto E = System::parseArguments({"sh", "/script.sh"});
    // Here we're going to put our shell script code into the input
    // stream of the process function, similar to how linux works
    E.in  = System::make_stream();
    E.out = System::make_stream();
    //E.in->set_eof();

    auto pid = M.runRawCommand(E);
    REQUIRE(pid == 1);
    auto exit_code = M.getProcessExitCode(pid);


    while(M.taskQueueExecute("PRE_MAIN") + M.taskQueueExecute("MAIN"));

    auto str = E.out->str();
    while(str.size() && str.back() == '\n')
        str.pop_back();
    return {str, *exit_code};
}

#if 1
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
            "sleep 1 && echo sleep finished &;"
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
#endif



SCENARIO("Test shell exit code")
{
    {
        auto [out, code] = testS(R"foo(
exit 0
)foo");
        REQUIRE(out == "");
        REQUIRE(code == 0);
    }

    {
        auto [out, code] = testS(R"foo(
exit 1
)foo");
        REQUIRE(out == "");
        REQUIRE(code == 1);
    }
}
#if 1
SCENARIO("Test the test command")
{
    {
        auto [out, code] = testS(R"foo(
test 32 = 32
)foo");
        REQUIRE(out == "");
        REQUIRE(code == 0);
    }
    {
        auto [out, code] = testS(R"foo(
test 32 = 31
)foo");
        REQUIRE(out == "");
        REQUIRE(code == 1);
    }

    {
        auto [out, code] = testS(R"foo(
test 32 != 32
)foo");
        REQUIRE(out == "");
        REQUIRE(code == 1);
    }
    {
        auto [out, code] = testS(R"foo(
test 32 != 31
)foo");
        REQUIRE(out == "");
        REQUIRE(code == 0);
    }
}

SCENARIO("Test Shell if-statements")
{
    {
        auto [out, code] = testS(R"foo(
if  true ; then
    echo "true"
fi
)foo");
        REQUIRE(out == "true");
        REQUIRE(code == 0);
    }

    {
        auto [out, code] = testS(R"foo(
if  false ; then
    echo "true"
fi
)foo");
        REQUIRE(out == "");
        REQUIRE(code == 0);
    }
}


SCENARIO("Test Shell if-else statements")
{
    {
        auto [out, code] = testS(R"foo(
if  true ; then
    echo "true"
else
    echo "false"
fi
)foo");
        REQUIRE(out == "true");
        REQUIRE(code == 0);
    }

    {
        auto [out, code] = testS(R"foo(
if  false ; then
    echo "true"
else
    echo "false"
fi
)foo");

        REQUIRE(out == "false");
        REQUIRE(code == 0);
    }
}


SCENARIO("Test Shell if-else statements")
{
    {
        auto [out, code] = testS(R"foo(
if  false ; then
    echo "false"
elif  true ; then
    echo "true"
else
    echo "false"
fi
)foo");
        REQUIRE(out == "true");
        REQUIRE(code == 0);
    }

    {
        auto [out, code] = testS(R"foo(
if  false ; then
    echo "true"
else
    echo "false"
fi
)foo");

        REQUIRE(out == "false");
        REQUIRE(code == 0);
    }
}


SCENARIO("Test Shell nested if-else statements")
{
    {
        auto [out, code] = testS(R"foo(
if  false ; then
    echo "false"
elif  true ; then
    echo true
    if  false ; then
        echo "false"
    elif  true ; then
        echo "true"
    else
        echo "false"
    fi
else
    echo "false"
fi
)foo");
        REQUIRE(out == "true\ntrue");
        REQUIRE(code == 0);
    }
}


SCENARIO("Test Task queue switching")
{
    {
        auto [out, code] = testS(R"foo(
echo $QUEUE
yield PRE_MAIN
echo $QUEUE
yield MAIN
echo $QUEUE
)foo");
        REQUIRE(out == "MAIN\nPRE_MAIN\nMAIN");
        REQUIRE(code == 0);
    }
}

#endif
