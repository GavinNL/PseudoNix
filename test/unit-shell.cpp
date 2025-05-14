#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <PseudoNix/System.h>
#include <PseudoNix/Shell.h>

using namespace PseudoNix;


SCENARIO("Tokenizer Generator")
{
    auto s = System::make_stream(R"foo(echo Hello $(get name))foo");
    s->set_eof();

    auto gn = bashTokenGenerator(s);

    std::vector<std::string> args;
    for(auto a : gn)
    {
        if(a)
        {
            args.push_back(*a);
        }
    }
    REQUIRE(args.size() == 5);
    REQUIRE(args[0] == "echo");
    REQUIRE(args[1] == "Hello");
    REQUIRE(args[2] == "$(get name)");
    REQUIRE(args[3] == ";");
    REQUIRE(args[4] == "done");
}


SCENARIO("Tokenizer Generator")
{
    auto s = System::make_stream(R"foo(ps
)foo");
//    s->set_eof();

    auto gn = bashTokenGenerator(s);
    auto a = gn.begin();
    std::cout << (*a).has_value() << std::endl;
    std::cout << *(*a) << std::endl;
}

SCENARIO("Tokenizer 3")
{
    std::stringstream ss;
    char c;
    ss.put('a');
    ss.put('b');
    ss.put('c');
    ss.get(c); REQUIRE(c == 'a');
    ss.get(c); REQUIRE(c == 'b');
    ss.get(c); REQUIRE(c == 'c');

    {
        auto v = Tokenizer::to_vector("\\$\\(sleep");
        REQUIRE(v[0] == "$(sleep");
    }

    {
        auto v = Tokenizer::to_vector("echo hello $(sleep 3 && echo world)");
        REQUIRE(v[0] == "echo");
        REQUIRE(v[1] == "hello");
        REQUIRE(v[2] == "$(sleep 3 && echo world)");
    }
    {
        auto v = Tokenizer::to_vector("echo $(get word) $(sleep $(get count) && echo world)");
        REQUIRE(v[0] == "echo");
        REQUIRE(v[1] == "$(get word)");
        REQUIRE(v[2] == "$(sleep $(get count) && echo world)");
    }

    {
        auto v = Tokenizer::to_vector("echo 1 && echo 2 || echo 3 #comment");
        REQUIRE(v[0] == "echo");
        REQUIRE(v[1] == "1");
        REQUIRE(v[2] == "&&");
        REQUIRE(v[3] == "echo");
        REQUIRE(v[4] == "2");
        REQUIRE(v[5] == "||");
        REQUIRE(v[6] == "echo");
        REQUIRE(v[7] == "3");
        REQUIRE(v[8] == "#");
        REQUIRE(v[9] == "comment");
    }
    {
        auto v = Tokenizer::to_vector("#echo hello world");
        REQUIRE(v[0] == "#");
        REQUIRE(v[1] == "echo");
        REQUIRE(v[2] == "hello");
        REQUIRE(v[3] == "world");
    }
    {
        auto v = Tokenizer::to_vector("sh -c \"echo hello world;\"");
        REQUIRE(v[0] == "sh");
        REQUIRE(v[1] == "-c");
        REQUIRE(v[2] == "echo hello world;");
    }
}

