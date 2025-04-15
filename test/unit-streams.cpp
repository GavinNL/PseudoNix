#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <PseudoNix/MiniLinux.h>
#include <PseudoNix/shell2.h>

using namespace PseudoNix;


SCENARIO("Tokenizer 3")
{
    auto stream = System::make_stream();
    using Stream = System::stream_type;

    char c;

    WHEN("We put some characters into the stream")
    {
        stream->put('c');

        // read the first character
        REQUIRE(stream->get(&c) == Stream::Result::SUCCESS);
        REQUIRE(c == 'c');
        REQUIRE(stream->get(&c) == Stream::Result::EMPTY);

        // set the end of file
        stream->put('c');
        stream->set_eof();
        // we only recieve eof
        REQUIRE(stream->get(&c) == Stream::Result::SUCCESS);
        REQUIRE(stream->get(&c) == Stream::Result::END_OF_STREAM);
        REQUIRE(stream->get(&c) == Stream::Result::EMPTY);
    }


}


SCENARIO("Tokenizer 3")
{
    auto stream = System::make_stream();
    using Stream = System::stream_type;

    WHEN("We put some characters into the stream")
    {
        std::string line;
        stream->put('a');
        stream->put('a');
        stream->put('\n');
        stream->put('b');
        stream->put('b');
        stream->put('\n');
        stream->put('c');
        stream->put('c');
        stream->set_eof();

        REQUIRE(Stream::Result::SUCCESS == stream->read_line(line));
        REQUIRE(line == "aa");
        line.clear();
        REQUIRE(Stream::Result::SUCCESS == stream->read_line(line));
        REQUIRE(line == "bb");

        line.clear();
        REQUIRE(Stream::Result::END_OF_STREAM == stream->read_line(line));
        REQUIRE(line == "cc");
    }


}
