#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <PseudoNix/System.h>
#include <PseudoNix/Shell.h>

using namespace PseudoNix;

SCENARIO("ReaderWriterStream")
{
    using Stream = ReaderWriterStream;

    GIVEN("A Stream")
    {
        Stream S;
        WHEN("We can place items into the stream")
        {
            S.put('a'); // add a single character
            S.put('b'); // add a single character
            S.put('c'); // add a single character
            S.set_eof(); // indicate that we have closed the stream.

            THEN("We can query the stream")
            {
                REQUIRE(S.has_data() == true);
                REQUIRE(S.size_approx() > 0);
                REQUIRE(S.eof() == false);
                REQUIRE(S.check() == Stream::Result::SUCCESS);
            }
            THEN("We can get from the stream")
            {
                char c=0;

                REQUIRE(S.has_data() == true);
                REQUIRE(S.check() == Stream::Result::SUCCESS);
                REQUIRE(S.get(&c) == Stream::Result::SUCCESS);
                REQUIRE(c == 'a');

                REQUIRE(S.has_data() == true);
                REQUIRE(S.get(&c) == Stream::Result::SUCCESS);
                REQUIRE(c == 'b');

                REQUIRE(S.has_data() == true);
                REQUIRE(S.get(&c) == Stream::Result::SUCCESS);
                REQUIRE(c == 'c');


                REQUIRE(S.check() == Stream::Result::END_OF_STREAM);
                REQUIRE(S.get(&c) == Stream::Result::END_OF_STREAM);
                REQUIRE(S.has_data() == false);

                // IF END_OF_STREAM is reached,
                // then stream is reopened
                REQUIRE(S.check() == Stream::Result::EMPTY);
                REQUIRE(S.get(&c) == Stream::Result::EMPTY);
            }
        }
    }
}

SCENARIO("Basic filling")
{
    using Stream = ReaderWriterStream;

    GIVEN("A Stream")
    {
        Stream S;

        WHEN("We can place items into the stream")
        {
            S << "Hello world\n";
            S << std::string("This is a test");
            S.set_eof();

            THEN("We can query the stream")
            {
                REQUIRE(S.has_data() == true);
                REQUIRE(S.size_approx() == 26);
                REQUIRE(S.eof() == false);
                REQUIRE(S.check() == Stream::Result::SUCCESS);
            }
            THEN("Then we can get the entire string")
            {
                std::string L = S.str();
                REQUIRE(L == "Hello world\nThis is a test");
            }
            THEN("Then we can get the entire string")
            {
                std::string L;
                S >> L;
                REQUIRE(L == "Hello world\nThis is a test");
            }
            THEN("Then we can read one line at a time")
            {
                std::string L;
                REQUIRE(Stream::Result::SUCCESS == S.read_line(L));
                REQUIRE(L=="Hello world");

                REQUIRE(Stream::Result::END_OF_STREAM == S.read_line(L));
                REQUIRE(L=="This is a test");

                // Empty
                REQUIRE(Stream::Result::SUCCESS == S.read_line(L));
            }
        }
    }
}


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
