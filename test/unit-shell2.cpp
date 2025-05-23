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
            E.in   = System::make_stream();
            E.out  = System::make_stream();
            //E.in->set_eof();
            return E;
        }
        else
        {
            auto E = System::parseArguments({"sh"});
            // Here we're going to put our shell script code into the input
            // stream of the process function, similar to how linux works
            E.in  = System::make_stream(script);
            E.out = System::make_stream();
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

#if 0
SCENARIO("Test single line")
{
    auto [out, code] = testS1(R"foo(
echo hello world
)foo");

    REQUIRE(out == "hello world");
    REQUIRE(code == 0);

}


SCENARIO("Test two lines line")
{
    auto [out, code] = testS1(R"foo(
echo hello world
echo hello world
)foo");

    REQUIRE(out == "hello world\nhello world");
    REQUIRE(code == 0);
}

SCENARIO("Test comments")
{
    auto [out, code] = testS1(R"foo(
#echo hello world
echo hello # world
)foo");

    REQUIRE(out == "hello");
    REQUIRE(code == 0);
}

SCENARIO("Test exit codes")
{
    auto [out, code] = testS1(R"foo(
exit 0
)foo");

    REQUIRE(out == "");
    REQUIRE(code == 0);
}

SCENARIO("Test exit codes")
{
    auto [out, code] = testS1(R"foo(
exit 1
)foo");

    REQUIRE(out == "");
    REQUIRE(code == 1);
}


SCENARIO("Test exit codes")
{
    auto [out, code] = testS1(R"foo(
exit 156
echo after exit
)foo");

    REQUIRE(out == "");
    REQUIRE(code == 156);
}

SCENARIO("Test Single if: true condition")
{
    auto [out, code] = testS1(R"foo(
if true; then
    echo true
fi
)foo");

    REQUIRE(out == "true");
    REQUIRE(code == 0);
}

SCENARIO("Test Single if: false condition")
{
    auto [out, code] = testS1(R"foo(
if false; then
    echo true
fi
)foo");

    REQUIRE(out == "");
    REQUIRE(code == 0);
}

SCENARIO("Test Single if-else: false condition")
{
    auto [out, code] = testS1(R"foo(
echo before
if true; then
    echo true
else
    echo false
fi
echo after
)foo");

    REQUIRE(out == "before\ntrue\nafter");
    REQUIRE(code == 0);
}

SCENARIO("Test if-else: false condition")
{
    auto [out, code] = testS1(R"foo(
echo before
if false; then
    echo true
else
    echo false
fi
echo after
)foo");

    REQUIRE(out == "before\nfalse\nafter");
    REQUIRE(code == 0);
}


SCENARIO("Test if-elif-else: false condition")
{
    auto [out, code] = testS1(R"foo(
echo before
if true; then
    echo if
elif true; then
    echo elif
else
    echo else
fi
echo after
)foo");

    REQUIRE(out == "before\nif\nafter");
    REQUIRE(code == 0);
}

SCENARIO("Test if-elif-else: false condition")
{
    auto [out, code] = testS1(R"foo(
echo before
if false; then
    echo if
elif true; then
    echo elif
else
    echo else
fi
echo after
)foo");

    REQUIRE(out == "before\nelif\nafter");
    REQUIRE(code == 0);
}

SCENARIO("Test if-elif-else: false condition")
{
    auto [out, code] = testS1(R"foo(
echo before
if false; then
    echo if
elif false; then
    echo elif
else
    echo else
fi
echo after
)foo");

    REQUIRE(out == "before\nelse\nafter");
    REQUIRE(code == 0);
}


SCENARIO("Test nested if-statement")
{
    auto [out, code] = testS1(R"foo(
echo before
if true; then
    echo if
    if true; then
        echo if
    elif false; then
        echo elif
    else
        echo else
    fi
elif false; then
    echo elif
else
    echo else
fi
echo after
)foo");

    REQUIRE(out == "before\nif\nif\nafter");
    REQUIRE(code == 0);
}


SCENARIO("Test While-loop")
{
    auto [out, code] = testS1(R"foo(
echo before
while false; do
    echo false
done
echo after
)foo");

    REQUIRE(out == "before\nafter");
    REQUIRE(code == 0);
}

SCENARIO("Test While-loop breaks")
{
    auto [out, code] = testS1(R"foo(
echo before
A=""
while true; do
    A=${A}A
    echo ${A}
    break
done
echo after
)foo");

    REQUIRE(out == "before\nA\nafter");
    REQUIRE(code == 0);
}

SCENARIO("Test While-loop breaks")
{
    auto [out, code] = testS1(R"foo(
echo before
A=""
while true; do
    A=${A}A
    echo ${A}
    if test ${A} = AAAA; then
        break
    fi
done
echo after
)foo");

    REQUIRE(out == "before\nA\nAA\nAAA\nAAAA\nafter");
    REQUIRE(code == 0);
}
#endif

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
