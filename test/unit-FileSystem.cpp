#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include<thread>
#include <PseudoNix/FileSystem.h>

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

SCENARIO("Exists")
{
    FileSystem F;

    REQUIRE(F.exists("/") == true);
    REQUIRE(F.exists("/home") == false);
}

SCENARIO("is_empty")
{
    FileSystem F;

    REQUIRE(F.is_empty("/") == true);
}

SCENARIO("mkdir")
{
    FileSystem F;

    F.mkdir("/home");
    F.mkdir("/usr");
    F.mkdir("/lib");

    REQUIRE(F.exists("/home") == true);
    REQUIRE(F.exists("/usr") == true);
    REQUIRE(F.exists("/lib") == true);

    REQUIRE(F.is_empty("/home") == true);
    REQUIRE(F.is_empty("/usr") == true);
    REQUIRE(F.is_empty("/lib") == true);

    REQUIRE(F.is_empty("/var").error() == FSResult::DOES_NOT_EXIST);
}

SCENARIO("touch")
{
    FileSystem F;

    F.mkdir("/home");
    REQUIRE(F.is_empty("/home") == true);

    F.touch("/home/file.txt");

    REQUIRE(F.exists("/home/file.txt"));
    REQUIRE(F.is_file("/home/file.txt"));

    REQUIRE(F.is_empty("/home/file2.txt").error() == FSResult::DOES_NOT_EXIST);
    REQUIRE(F.is_file("/home/file2.txt").error() == FSResult::DOES_NOT_EXIST);
}

SCENARIO("Custom file")
{
    FileSystem F;

    REQUIRE(F.mkcustom("/test") == true);

    REQUIRE_NOTHROW( F.get<NodeCustom>("/test") );
    F.get<NodeCustom>("/test").data = 32;
    REQUIRE( std::any_cast<int>(F.get<NodeCustom>("/test").data) == 32);
    REQUIRE( F.get<NodeCustom>("/test").is<int>() == true);
    REQUIRE( F.get<NodeCustom>("/test").as<int>() == 32);
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

        WHEN("We mount a host directory")
        {
            REQUIRE(F.mount(CMAKE_SOURCE_DIR, "/src"  ));
            REQUIRE(F.mount(CMAKE_BINARY_DIR, "/build"));

            // cannot mount on the same spot
            REQUIRE(F.mount(CMAKE_SOURCE_DIR, "/src").error() == FSResult::NOT_VALID_MOUNT);

            THEN("We can query the files as if they were in the VFS")
            {
                REQUIRE(F.exists("/src/conanfile.py"));
                REQUIRE(F.is_file("/src/conanfile.py"));
                REQUIRE(F.is_dir("/src/examples"));

                REQUIRE(F.host_path("/src/conanfile.py") == CMAKE_SOURCE_DIR "/conanfile.py");
            }
            THEN("We can loop through all the folders")
            {
                for(auto  i : F.list_dir("/build"))
                {
                    (void)i;
                    //std::cout << i << std::endl;
                }
            }

            THEN("We can create files on the host through the vfs")
            {
                std::filesystem::remove_all(CMAKE_BINARY_DIR "/test-folder");

                REQUIRE(F.mkdir("/build/test-folder") == true);
                REQUIRE(F.exists("/build/test-folder") == true);
                REQUIRE(F.exists("/build/test-folder/noexist.txt") == false);
                REQUIRE(F.is_file("/build/test-folder/noexist.txt") == false);

                // Cannot mount within a host tree
                REQUIRE(F.mount(CMAKE_SOURCE_DIR, "/build/test-folder").error() == FSResult::NOT_VALID_MOUNT);
                {
                    REQUIRE(std::filesystem::is_directory(CMAKE_BINARY_DIR "/test-folder"));
                    REQUIRE(F.is_empty("/build/test-folder") == true);
                }

                {
                    F.touch("/build/test-folder/file.txt");

                    REQUIRE(std::filesystem::is_regular_file(CMAKE_BINARY_DIR "/test-folder/file.txt"));
                    REQUIRE(F.exists("/build/test-folder/file.txt") == true);
                    REQUIRE(F.is_file("/build/test-folder/file.txt") == true);
                    REQUIRE(F.is_dir("/build/test-folder/file.txt") == false);
                }
            }
        }
    }
}


SCENARIO("Opening Files")
{
    FileSystem F;
    F.mkdir("/src");
    F.mkdir("/bin");
    F.mount(CMAKE_SOURCE_DIR, "/src");
    F.mount(CMAKE_BINARY_DIR, "/bin");

    REQUIRE(F.touch("/test.txt") == true);

    REQUIRE_NOTHROW( F.get<NodeFile>("/test.txt") );
    F.fs("/test.txt") << "Hello world\nGoodbye world";

    std::string str;

    THEN("test")
    {
        std::stringstream SS;
        SS.str("Hello world");
        SS >> str;
        REQUIRE( str == "Hello" );
        REQUIRE( SS.str() == "Hello world");
    }
    THEN("We can open the host file")
    {
        auto f = F.open("/src/conanfile.py", std::ios::in);
        REQUIRE(f);
        std::getline(f, str);
        REQUIRE(str == "from conan.tools.files import copy");
        std::getline(f, str);
        REQUIRE(str == "from conan import ConanFile");
    }
    THEN("We can open the memory file")
    {
        auto f = F.open("/test.txt", std::ios::in);
        REQUIRE(f);
        std::getline(f, str);
        REQUIRE(str == "Hello world");

        f >> str;
        REQUIRE(str == "Goodbye");
    }
    THEN("We can open the memory file")
    {
        F.touch("/bin/test.txt");
        auto f = F.open("/bin/test.txt", std::ios::app | std::ios::out);
        REQUIRE(f);

        std::ofstream FF("/asdfasdf");
        f << "test\n";
        f << 32 << '\n';
    }
}
