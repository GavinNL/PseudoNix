#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

//#define PSEUDONIX_LOG_LEVEL_SYSTEM
#include <PseudoNix/System.h>
#include <PseudoNix/Shell.h>

using namespace PseudoNix;


std::pair<std::string, System::exit_code_type> testS1(std::string script, bool from_file = false)
{
    System M;

    M.taskQueueCreate("PRE_MAIN");
    M.setFunction("sh", shell_coro);

    M.mkfile("/script.sh");
    M.fs("/script.sh") << script;

    auto E1 = [&]()
    {
        if(from_file)
        {
            auto E = System::parseArguments({"sh", "/script.sh"});
            E.in   = System::makeStream();
            E.out  = System::makeStream();
            //E.in->set_eof();
            return E;
        }
        else
        {
            auto E = System::parseArguments({"sh"});
            // Here we're going to put our shell script code into the input
            // stream of the process function, similar to how linux works
            E.in   = System::makeStream(script);
            E.out  = System::makeStream();
            E.in->set_eof();
            return E;
        }
    }();

    auto pid = M.runRawCommand(E1);
    REQUIRE(pid == 1);
    auto exit_code = M.getProcessExitCode(pid);

    while(M.taskQueueExecute("PRE_MAIN") + M.taskQueueExecute("MAIN"));

    auto str = E1.out->str();
    while(str.size() && str.back() == '\n')
        str.pop_back();
    return {str, *exit_code};
}

SCENARIO("Test While-loop breaks")
{
    auto [out, code] = testS1(R"foo(
echo before
A=""
while true; do
    A=${A}A
    if test ${A} = AAA; then
        echo CONTINUE
        continue
        echo AFTER_CONTINUE
    fi
    if test ${A} = AAAAAA; then
        break
    fi
    echo ${A}
done
echo after
)foo");

    REQUIRE(out == "before\nA\nAA\nCONTINUE\nAAAA\nAAAAA\nafter");
    REQUIRE(code == 0);
}
#if 1
SCENARIO("Test For-loop")
{
    auto [out, code] = testS1(R"foo(
echo before
for A in hello world; do
    echo ${A}
done
echo after
)foo", true);

    REQUIRE(out == "before\nhello\nworld\nafter");
    REQUIRE(code == 0);
}

SCENARIO("Test For-loop break")
{
    auto [out, code] = testS1(R"foo(
echo before
for A in hello world; do
    echo ${A}
    if test ${A} = hello; then
        break
    fi
done
echo after
)foo", true);

    REQUIRE(out == "before\nhello\nafter");
    REQUIRE(code == 0);
}



SCENARIO("Test Queue")
{
    auto [out, code] = testS1(R"foo(
echo ${QUEUE}
yield PRE_MAIN
echo ${QUEUE}
yield MAIN
echo ${QUEUE}
)foo", false);

    REQUIRE(out == "MAIN\nPRE_MAIN\nMAIN");
    REQUIRE(code == 0);
}


SCENARIO("Test File-system")
{
    auto [out, code] = testS1(R"foo(

mkdir /test_dir
if test -d /test_dir; then
    echo dir
fi

touch /test_file
if test -f /test_file; then
    echo file
fi

if test -e /test_dir; then
    echo direxists
fi

if test -e /test_file; then
    echo fileexists
fi

rm /test_dir
rm /test_file

if test ! -e /test_dir; then
    echo rmdir
fi

if test ! -e /test_file; then
    echo rmfile
fi

)foo", false);

    REQUIRE(out == "dir\nfile\ndirexists\nfileexists\nrmdir\nrmfile");
    REQUIRE(code == 0);
}

SCENARIO("Test if statements using [[  ]] ")
{
    auto [out, code] = testS1(R"foo(

mkdir /test_dir

if [[ -d /test_dir ]]; then
    echo exists
fi
)foo", false);

    REQUIRE(out == "exists");
    REQUIRE(code == 0);
}


SCENARIO("Test if statements using [[  ]] ")
{
    auto [out, code] = testS1(R"foo(
sleep 2 && mkdir /test_dir &
while [[ ! -d /test_dir ]]; do
    echo exists
    sleep 1
done

)foo", false);

    REQUIRE(out == "2\nexists\nexists");
    REQUIRE(code == 0);
}


#endif
