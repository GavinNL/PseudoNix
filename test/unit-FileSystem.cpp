#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <PseudoNix/FileSystem.h>

#include <format>

using namespace PseudoNix;

bool mkdir(FileSystem &F, std::filesystem::path const & p)
{
    REQUIRE(F.mkdir(p) == true);
    REQUIRE(F.is_dir(p) == true);
    REQUIRE(F.is_file(p) == false);
    REQUIRE(F.is_empty(p) == true);
    REQUIRE(F.exists(p) == true);
    return true;
}

SCENARIO("mkdir")
{
    FileSystem F;

    REQUIRE(mkdir(F, "/home") == true);


    REQUIRE(F.mkdir("/home/A") == true);
    REQUIRE(F.mkdir("/home/C") == true);
    REQUIRE(F.mkdir("/home/B") == true);

    REQUIRE(F.mkdir("/home/B/a") == true);
    REQUIRE(F.mkdir("/home/B/b") == true);
    REQUIRE(F.mkdir("/home/B/c") == true);
    REQUIRE(F.mkdir("/home/B/d") == true);
}

SCENARIO("dd")
{
    FileSystem F;

    {
        std::filesystem::path p("/");
        _clean(p);
        REQUIRE(p == "/");
    }
    {
        std::filesystem::path p("/home/");
        _clean(p);
        REQUIRE(p == "/home");
    }


    REQUIRE(F.exists("/"));
    REQUIRE(F.is_dir("/"));

    F.mkdir("/home/gavin/");
    F.mkdir("/home/gavin/Documents");
    F.mkdir("/home/gavin/Projects");
    REQUIRE(F.exists("/home"));
    REQUIRE(F.exists("/home/"));
    REQUIRE(F.is_dir("/home"));
    REQUIRE(F.is_dir("/home/"));
    REQUIRE(F.exists("/home/gavin"));
    REQUIRE(F.exists("/home/gavin/"));

    REQUIRE(F.is_empty("/home/gavin") == false);
    REQUIRE(F.is_empty("/home/gavin/Documents") == true);


    F.touch("/home/gavin/bashrc");
    F.touch("/home/gavin/profile");

    REQUIRE(F.exists("/home/gavin"));
    REQUIRE(F.exists("/home/gavin/"));

    REQUIRE(F.exists("/home/gavin/bashrc"));
    REQUIRE(F.exists("/home/gavin/profile"));

    REQUIRE(F.mkdir("/home/gavin/profile/test") == false);
}

SCENARIO("Mount")
{
    FileSystem F;

    GIVEN("A filesystem and a directory")
    {
        REQUIRE(F.exists("/"));
        REQUIRE(F.mkdir("/src"));
        REQUIRE(F.mkdir("/build"));
        REQUIRE(F.mkdir("/a/b/c/d"));

        REQUIRE(F.find_parent_mount("/a/b/c/d") == "");

        WHEN("We mount a host directory")
        {
            REQUIRE(F.mount("/src", CMAKE_SOURCE_DIR));
            REQUIRE(F.mount("/build", CMAKE_BINARY_DIR));

            THEN("We can query the files as if they were in the VFS")
            {
                REQUIRE(F.exists("/src/conanfile.py"));
                REQUIRE(F.is_file("/src/conanfile.py"));
                REQUIRE(F.is_dir("/src/examples"));
            }
            THEN("We can loop through all the folders")
            {
                for(auto  i : F.list_dir("/build"))
                {
                    std::cout << i << std::endl;
                }
            }

            THEN("We can create files on the host through the vfs")
            {
                std::filesystem::remove(CMAKE_BINARY_DIR "/test-folder");
                REQUIRE(F.mkdir("/build/test-folder") == true);
                REQUIRE(F.exists("/build/test-folder") == true);

                REQUIRE(std::filesystem::is_directory(CMAKE_BINARY_DIR "/test-folder"));
                REQUIRE(F.is_empty("/build/test-folder") == true);
            }
        }
    }
}

