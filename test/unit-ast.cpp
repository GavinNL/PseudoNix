#include <catch2/catch_all.hpp>
#include <catch2/benchmark/catch_benchmark_all.hpp>
#include <fmt/format.h>
#include <ebash/MiniLinux.h>
#include <ebash/shell.h>

using namespace bl;





void printAST(std::shared_ptr<AstNode> d, std::string indent)
{
    std::cout << indent << d->value << std::endl;;
    if(d->left)
        printAST(d->left, indent + "  ");
    if(d->right)
        printAST(d->right, indent + "  ");
}

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

}
