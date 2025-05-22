#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <PseudoNix/FileSystem.h>
#include <PseudoNix/HostMount.h>

using namespace PseudoNix;

SCENARIO("Test Write and expand")
{
    std::vector<char> data(5);
    auto p = std::make_unique<VectorBackedStreamBuf>(data, std::ios::out );

    WHEN("We write a 5-letter world")
    {
        FileStream out( std::make_unique<VectorBackedStreamBuf>(data, std::ios::out ) );
        out << "hello";
        THEN("nothing happens to the data")
        {
            REQUIRE(data.size() == 5);
            REQUIRE(std::string("hello") == std::string_view(data.data(), data.size()));

            WHEN("We write a single character")
            {
                out << '!';

                THEN("The size of the container expands")
                {
                    REQUIRE(data.size() > 5);

                    WHEN("We flush the data")
                    {
                        out.flush();
                        out.flush();
                        out.flush();
                        out.flush();

                        THEN("THe vector is shrunk to fit")
                        {
                            REQUIRE(data.size() == 6);
                            REQUIRE(std::string("hello!") == std::string_view(data.data(), data.size()));
                        }
                    }
                }
            }
        }
    }
}

SCENARIO("Test Write/Append")
{
    std::vector<char> data;
    auto p = std::make_unique<VectorBackedStreamBuf>(data, std::ios::out );

    THEN("We can write multiple lines to it")
    {
        {
            FileStream out( std::make_unique<VectorBackedStreamBuf>(data, std::ios::out ) );
            out << "Hello world";
        }

        REQUIRE(std::string("Hello world") == std::string_view(data.data(), data.size()));

        THEN("over-write")
        {
            {
                std::string line;
                FileStream out(std::make_unique<VectorBackedStreamBuf>(data, std::ios::out ));
                out << "Goodbye world";
            }
            REQUIRE(std::string("Goodbye world") == std::string_view(data.data(), data.size()));
        }
        THEN("append")
        {
            {
                std::string line;
                FileStream out(std::make_unique<VectorBackedStreamBuf>(data, std::ios::out | std::ios::app ));
                out << "Goodbye world";
            }
            REQUIRE(std::string("Hello worldGoodbye world") == std::string_view(data.data(), data.size()));
        }
    }
}

SCENARIO("Test Read")
{
    std::vector<char> data;
    auto p = std::make_unique<VectorBackedStreamBuf>(data, std::ios::out );

    THEN("We can write multiple lines to it")
    {
        {
            oFileStream out( std::make_unique<VectorBackedStreamBuf>(data, std::ios::out ) );
            out << "Hello ";
            out << 32.432;
        }

        REQUIRE(std::string("Hello 32.432") == std::string_view(data.data(), data.size()));

        {
            iFileStream in( std::make_unique<VectorBackedStreamBuf>(data, std::ios::in) );
            std::string str;
            float f = 0.0f;
            in >> str;
            in >> f;

            REQUIRE(str == "Hello");
            REQUIRE(f >= 32.4f);
        }

    }
}


SCENARIO("Test")
{
    std::vector<char> data;
    auto p = std::make_unique<VectorBackedStreamBuf>(data, std::ios::out );

    THEN("We can write multiple lines to it")
    {
        {
            FileStream out( std::make_unique<VectorBackedStreamBuf>(data, std::ios::out ) );
            out << "Hello world.\n";
            out << "Hello";
            out << "World";
        }

        REQUIRE(std::string("Hello world.\nHelloWorld") == std::string_view(data.data(), data.size()));

        THEN("We can read the lines")
        {
            std::string line;
            FileStream in(std::make_unique<VectorBackedStreamBuf>(data, std::ios::in ));
            std::getline(in, line);
            REQUIRE(line == "Hello world.");
            std::getline(in, line);
            REQUIRE(line == "HelloWorld");
        }
        THEN("We can read the lines")
        {
            std::string line;
            FileStream in(std::make_unique<VectorBackedStreamBuf>(data, std::ios::in ));
            while(!in.eof())
            {
                std::getline(in, line);
                std::cout << line << std::endl;
            }
        }
    }
}

SCENARIO("asdf")
{
    std::vector<char> data(3);

    {
        auto p = std::make_unique<VectorBackedStreamBuf>(data, std::ios::out );
        {
            FileStream out( std::move(p) );
            out << "1234567890abcdefghijklmnopqrstuvwxyz";
        }

        auto s = std::string_view(data.data(), data.size());
        std::cout << s << std::endl;
    }
    {
        auto p = std::make_unique<VectorBackedStreamBuf>(data, std::ios::out | std::ios::app );
        {
            FileStream out( std::move(p) );
            out << "ABCDEFGHIJKLMNOPQRS" << std::flush;
        }

        auto s = std::string_view(data.data(), data.size());
        std::cout << s << std::endl;
    }
    {
        auto p = std::make_unique<VectorBackedStreamBuf>(data, std::ios::out );

        {
            FileStream out( std::move(p) );
            out << "abcdefgh";
        }

        auto s = std::string_view(data.data(), data.size());
        std::cout << s << std::endl;
    }

    {
        auto p = std::make_unique<VectorBackedStreamBuf>(data, std::ios::out | std::ios::app );
        FileStream out( std::move(p) );

        out << "67890";

        auto s = std::string_view(data.data(), data.size());
        std::cout << s << std::endl;
    }
}
