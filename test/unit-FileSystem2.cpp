#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <PseudoNix/FileSystem2.h>

using namespace PseudoNix;


SCENARIO("split_first")
{
    {
        std::filesystem::path p = "path/to/foo.txt";
        auto [first, rem] = split_first(p);
        REQUIRE(first == "path");
        REQUIRE(rem == "to/foo.txt");
    }
    {
        std::filesystem::path p = "path";
        auto [first, rem] = split_first(p);
        REQUIRE(first == "path");
        REQUIRE(rem.empty());
    }
    {
        std::filesystem::path p = "path/";
        auto [first, rem] = split_first(p);
        REQUIRE(first == "path");
        REQUIRE(rem.empty());
    }
}

SCENARIO("find_last_valid_virtual_node")
{
    FileSystem2 F;

    REQUIRE(F.exists("/") == FSResult2::True);

    {
        auto [node, rem] = F.find_last_valid_virtual_node("/hello");
        REQUIRE(node == F.m_rootNode);
        REQUIRE( rem == "hello");
    }
    {
        auto [node, rem] = F.find_last_valid_virtual_node("/hello/world");
        REQUIRE(node == F.m_rootNode);
        REQUIRE( rem == "hello/world");
    }

    {
        auto hello = std::make_shared<FSNodeDir>("hello");
        F.m_rootNode->nodes["hello"] = hello;
        auto [node, rem] = F.find_last_valid_virtual_node("/hello/world");
        REQUIRE(node == hello);
        REQUIRE( rem == "world");
    }

    REQUIRE(F.exists("/hello") == FSResult2::True);
    REQUIRE(F.exists("/hello2") == FSResult2::False);
}

SCENARIO("Exists")
{
    FileSystem2 F;

    REQUIRE(F.exists("/") == FSResult2::True);
    REQUIRE(F.exists("/hello") == FSResult2::False);
}

SCENARIO("mkdir")
{
    GIVEN("A an empty filesystem")
    {
        FileSystem2 F;
        REQUIRE(F.exists("/") == FSResult2::True);

        THEN("We can check if folders dont exist")
        {
            REQUIRE(F.exists("/bye") == FSResult2::False);
        }
        THEN("We cannot create mulitple directories")
        {
            REQUIRE(F.mkdir("/hello/world") == FSResult2::False);
        }
        THEN("We can create a directory")
        {
            REQUIRE(F.exists("/hello") == FSResult2::False);
            REQUIRE(F.mkdir("/hello") == FSResult2::True);
            REQUIRE(F.exists("/hello") == FSResult2::True);

            THEN("We can create a sub directory")
            {
                REQUIRE(F.mkdir("/hello/world") == FSResult2::True);
                REQUIRE(F.exists("/hello/world") == FSResult2::True);
            }
        }
    }
}


SCENARIO("mkfile")
{
    GIVEN("A an empty filesystem")
    {
        FileSystem2 F;
        REQUIRE(F.exists("/") == FSResult2::True);

        THEN("We can create a file in the root folder")
        {
            REQUIRE(F.exists("/hello") == FSResult2::False);
            REQUIRE(F.mkfile("/hello") == FSResult2::True);
            REQUIRE(F.exists("/hello") == FSResult2::True);

            THEN("We cannot create the file again")
            {
                REQUIRE(F.mkfile("/hello") == FSResult2::False);
            }
            THEN("We can create a file in a subfolder")
            {
                REQUIRE(F.mkdir("/hello2") == FSResult2::True);
                REQUIRE(F.mkdir("/hello2/world") == FSResult2::True);

                REQUIRE(F.mkfile("/hello2/world/file") == FSResult2::True);

                REQUIRE(F.exists("/hello2/world/file") == FSResult2::True);
            }
        }
    }
}

SCENARIO("rm")
{
    GIVEN("A filesystem with some directories and files")
    {
        FileSystem2 F;
        REQUIRE(F.exists("/") == FSResult2::True);

        THEN("We can remove a single directory")
        {
            REQUIRE(F.mkdir("/bin"));
            REQUIRE(F.rm("/bin") == FSResult2::True);
            REQUIRE(F.exists("/bin") == FSResult2::False);
        }

        THEN("We cannot delete non-empty folders")
        {
            REQUIRE(F.mkdir("/etc"));
            REQUIRE(F.mkfile("/etc/profile.txt"));
            REQUIRE(F.rm("/etc") == FSResult2::False);

            WHEN("We make the folder empty")
            {
                REQUIRE(F.rm("/etc/profile.txt") == FSResult2::True);
                REQUIRE(F.exists("/etc/profile.txt") == FSResult2::False);

                THEN("We can delete the folder")
                {
                    REQUIRE(F.rm("/etc") == FSResult2::True);
                    REQUIRE(F.exists("/etc") == FSResult2::False);
                }
            }
        }
    }
}


SCENARIO("mount")
{
    GIVEN("A filesystem with some directories and files")
    {
        FileSystem2 F;

        THEN("We can mount a directory")
        {
            REQUIRE(F.exists("/") == FSResult2::True);
            REQUIRE(F.mkdir("/src") == FSResult2::True);
            REQUIRE( FSResult2::True == F.mount<FSNodeHostMount>("/src", CMAKE_SOURCE_DIR));

            REQUIRE(F.exists("/src/conanfile.py") == FSResult2::True);
            REQUIRE(F.exists("/src/noexist.py") == FSResult2::False);

            REQUIRE(F.exists("/src/test") == FSResult2::True);
            REQUIRE(F.exists("/src/test/CMakeLists.txt") == FSResult2::True);

            THEN("We can unmount a directory")
            {
                REQUIRE( FSResult2::True == F.unmount("/src"));

                REQUIRE(F.exists("/src/conanfile.py") == FSResult2::False);
                REQUIRE(F.exists("/src/noexist.py")   == FSResult2::False);

                REQUIRE(F.exists("/src/test") == FSResult2::False);
                REQUIRE(F.exists("/src/test/CMakeLists.txt") == FSResult2::False);

            }
        }
    }
}

SCENARIO("mount with file manipulation")
{
    GIVEN("A filesystem with some directories and files")
    {
        FileSystem2 F;

        REQUIRE(F.exists("/") == FSResult2::True);
        REQUIRE(F.mkdir("/build") == FSResult2::True);
        REQUIRE( FSResult2::True == F.mount<FSNodeHostMount>("/build", CMAKE_BINARY_DIR));
        REQUIRE(F.exists("/build/CMakeCache.txt") == FSResult2::True);

        std::filesystem::remove_all(CMAKE_BINARY_DIR "/test-folder");

        THEN("Create a directory on the host through the VFS mount")
        {
            REQUIRE(FSResult2::True == F.mkdir("/build/test-folder"));
            REQUIRE(std::filesystem::exists(CMAKE_BINARY_DIR "/test-folder"));
            REQUIRE(FSResult2::True == F.exists("/build/test-folder"));

            THEN("Create a file on the host through the VFS mount")
            {
                REQUIRE(FSResult2::True == F.mkfile("/build/test-folder/test.file"));
                REQUIRE(std::filesystem::exists(CMAKE_BINARY_DIR "/test-folder/test.file"));
                REQUIRE(FSResult2::True == F.exists("/build/test-folder/test.file"));
            }
        }
        std::filesystem::remove_all(CMAKE_BINARY_DIR "/test-folder");
    }
}

SCENARIO("write to files")
{
    GIVEN("A filesystem with some directories and files")
    {
        FileSystem2 F;
        REQUIRE(F.exists("/") == FSResult2::True);
        REQUIRE(F.mkdir("/folder") == FSResult2::True);
        REQUIRE(F.mkfile("/folder/file.txt") == FSResult2::True);

        {
            auto out = F.open("/folder/file.txt", std::ios::out);
            out << "Hello";
        }

        {
            std::string str;
            auto in = F.open("/folder/file.txt", std::ios::in);
            in >> str;
            REQUIRE(str == "Hello");
        }
    }
}


SCENARIO("Remove files/directories")
{
    GIVEN("A filesystem with some directories and files")
    {
        FileSystem2 F;
        REQUIRE(F.exists("/") == FSResult2::True);
        REQUIRE(F.mkdir("/folder") == FSResult2::True);
        REQUIRE(F.mkdir("/folder/A") == FSResult2::True);
        REQUIRE(F.mkfile("/folder/B") == FSResult2::True);
        REQUIRE(F.mkfile("/C") == FSResult2::True);
        REQUIRE(F.mkdir("/D") == FSResult2::True);

        REQUIRE(F.exists("/folder") == FSResult2::True);
        REQUIRE(F.exists("/folder/A") == FSResult2::True);
        REQUIRE(F.exists("/folder/B") == FSResult2::True);
        REQUIRE(F.exists("/C") == FSResult2::True);
        REQUIRE(F.exists("/D") == FSResult2::True);


        // not empty
        REQUIRE(F.rm("/folder") == FSResult2::False);
        REQUIRE(F.rm("/folder/A") == FSResult2::True);
        REQUIRE(F.exists("/folder/A") == FSResult2::False);

        REQUIRE(F.rm("/folder/B") == FSResult2::True);
        REQUIRE(F.exists("/folder/B") == FSResult2::False);

        REQUIRE(F.rm("/C") == FSResult2::True);
        REQUIRE(F.rm("/D") == FSResult2::True);
        REQUIRE(F.exists("/C") == FSResult2::False);
        REQUIRE(F.exists("/D") == FSResult2::False);

        REQUIRE(F.rm("/C/does/not/exist") == FSResult2::False);
    }
}



SCENARIO("Remove files/directories on host")
{
    GIVEN("A filesystem with some directories and files")
    {
        FileSystem2 F;
        REQUIRE(F.exists("/") == FSResult2::True);

        std::filesystem::remove_all(CMAKE_BINARY_DIR "/folder");
        std::filesystem::create_directories(CMAKE_BINARY_DIR "/folder");

        REQUIRE(F.mkdir("/folder") == FSResult2::True);
        REQUIRE( FSResult2::True == F.mount<FSNodeHostMount>("/folder", CMAKE_BINARY_DIR "/folder"));

        REQUIRE(F.mkdir("/folder/A") == FSResult2::True);
        REQUIRE(F.mkfile("/folder/B") == FSResult2::True);
        REQUIRE(F.mkfile("/C") == FSResult2::True);
        REQUIRE(F.mkdir("/D") == FSResult2::True);

        REQUIRE(F.exists("/folder") == FSResult2::True);
        REQUIRE(F.exists("/folder/A") == FSResult2::True);
        REQUIRE(F.exists("/folder/B") == FSResult2::True);
        REQUIRE(F.exists("/C") == FSResult2::True);
        REQUIRE(F.exists("/D") == FSResult2::True);


        // not empty
        REQUIRE(F.rm("/folder") == FSResult2::False);
        REQUIRE(F.rm("/folder/A") == FSResult2::True);
        REQUIRE(F.exists("/folder/A") == FSResult2::False);

        REQUIRE(F.rm("/folder/B") == FSResult2::True);
        REQUIRE(F.exists("/folder/B") == FSResult2::False);

        REQUIRE(F.rm("/C") == FSResult2::True);
        REQUIRE(F.rm("/D") == FSResult2::True);
        REQUIRE(F.exists("/C") == FSResult2::False);
        REQUIRE(F.exists("/D") == FSResult2::False);

        REQUIRE(F.rm("/C/does/not/exist") == FSResult2::False);
    }
}
