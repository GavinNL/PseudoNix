#include <catch2/catch_all.hpp>
#include <catch2/benchmark/catch_benchmark_all.hpp>
#include <fmt/format.h>
#include <ebash/MiniLinux.h>


using namespace bl;

#if 0

SCENARIO("MiniLinux: sh - pipe")
{
    {
        auto top = bl::generateTree("ls -l && echo hello world | grep hello | grep world || false && grep asdf");

        printAST(top, "");
    }

    {
        auto top = bl::generateTree("ls -l | grep hello");

        printAST(top, "");
    }

    {
        auto top = bl::generateTree("ls -l & grep hello");

        printAST(top, "");
    }

    {
        auto top = bl::generateTree("");

        printAST(top, "");
    }


    {
        auto top = bl::generateTree("X=test echo hello world && echo helo");

        printAST(top, "");
    }


}
#endif
