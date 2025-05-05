#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <PseudoNix/FileSystem.h>

using namespace PseudoNix;

SCENARIO("Test FS linux/windows")
{
    using path = std::filesystem::path;

    
    auto _test = [](path const& P) {
        std::cout << "String: " << P.string() << std::endl;
        std::cout << "Generic String: " << P.generic_string() << std::endl;
        std::cout << "root_name: " << P.root_name() << std::endl;
        std::cout << "root_directory: " << P.root_directory() << std::endl ;
        std::cout << "root_path: " << P.root_path() << std::endl;

        std::cout << "relative_path: " << P.relative_path() << std::endl;
        std::cout << std::endl;
    };

    _test(std::string("/home/gavin"));
    _test(std::string("home/gavin"));
    _test(std::string("./home"));

    _test(std::string("C:"));
    _test(std::string("C:/"));
    _test(std::string("C:\\home"));
    _test(std::string("C:/home"));

    auto _isAbsolute = [](path const& P)
    {
        return P.has_root_directory();
    };

    auto CLEAN = [](path P)
    {
        _clean(P);
        return P;
    };

    REQUIRE(_isAbsolute("/home"));
    REQUIRE(_isAbsolute("/"));
    REQUIRE(CLEAN("home") == "home");
    REQUIRE(CLEAN("/home") == "/home");

    REQUIRE(CLEAN("/home") == "/home");
    REQUIRE(CLEAN("/home").has_root_directory() == true);
    

    REQUIRE(CLEAN("/home/") == "/home");

    REQUIRE(CLEAN("home/") == "home");
    REQUIRE(CLEAN("/home/") == "/home");

    REQUIRE(CLEAN("\\home\\gavin\\").generic_string() == "/home/gavin");
    REQUIRE(CLEAN("\\home\\gavin\\").has_root_directory() == true);

    REQUIRE(CLEAN("\\home\\\\gavin").generic_string() == "/home/gavin");
    REQUIRE(CLEAN("\\home\\\\gavin").has_root_directory() == true);

    REQUIRE(CLEAN("/home/gavin").generic_string() == "/home/gavin");
    REQUIRE(CLEAN("/home/gavin").has_root_directory() == true);

    REQUIRE(CLEAN("/home///gavin").generic_string() == "/home/gavin");
    REQUIRE(CLEAN("/home///gavin").has_root_directory() == true);

    REQUIRE(CLEAN("/home/./gavin").generic_string() == "/home/gavin");
    REQUIRE(CLEAN("/home/./gavin").has_root_directory() == true);

    REQUIRE(CLEAN("/home/../home/gavin").generic_string() == "/home/gavin");
    REQUIRE(CLEAN("/home/../home/gavin").has_root_directory() == true);

    REQUIRE(CLEAN("\\home\\..\\home\\gavin").generic_string() == "/home/gavin");
    REQUIRE(CLEAN("\\home\\..\\home\\gavin").has_root_directory() == true);

    REQUIRE(CLEAN("/home/gavin/").generic_string() == "/home/gavin");
    REQUIRE(CLEAN("/home/gavin/").has_root_directory() == true);

}

#if 1
bool mkdir(FileSystem &F, std::filesystem::path const & p)
{
    REQUIRE(F.mkdir(p)    == FSResult::Success);
    REQUIRE(F.is_dir(p)   == true);
    REQUIRE(F.is_file(p)  == false);
    REQUIRE(F.is_empty(p) == FSResult::True);
    REQUIRE(F.exists(p)   == true);
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

    REQUIRE(F.is_empty("/") == FSResult::True);
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

    REQUIRE(F.is_empty("/home") == FSResult::True);
    REQUIRE(F.is_empty("/usr") == FSResult::True);
    REQUIRE(F.is_empty("/lib") == FSResult::True);

    REQUIRE(F.is_empty("/var") == FSResult::DoesNotExist);
}

SCENARIO("touch")
{
    FileSystem F;

    F.mkdir("/home");
    REQUIRE(F.is_empty("/home") == FSResult::True);

    F.touch("/home/file.txt");

    REQUIRE(F.exists("/home/file.txt"));
    REQUIRE(F.is_file("/home/file.txt"));

    REQUIRE(F.is_empty("/home/file2.txt") == FSResult::DoesNotExist);
    REQUIRE(F.is_file("/home/file2.txt") == false);
}

SCENARIO("Custom file")
{
    FileSystem F;

    REQUIRE(F.mkcustom("/test") == FSResult::Success);

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
        REQUIRE(F.mkdir("/src") == FSResult::Success);
        REQUIRE(F.mkdir("/build") == FSResult::Success );
        REQUIRE(F.mkdir("/a/b/c/d") == FSResult::Success);

        WHEN("We mount a host directory")
        {
            REQUIRE(F.mount(CMAKE_SOURCE_DIR, "/src"  ) == FSResult::Success);
            REQUIRE(F.mount(CMAKE_BINARY_DIR, "/build") == FSResult::Success);

            // cannot mount on the same spot
            REQUIRE(F.mount(CMAKE_SOURCE_DIR, "/src") == FSResult::NotValidMount);

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

                REQUIRE(F.mkdir("/build/test-folder") == FSResult::Success);
                REQUIRE(F.exists("/build/test-folder") == true);
                REQUIRE(F.exists("/build/test-folder/noexist.txt") == false);
                REQUIRE(F.is_file("/build/test-folder/noexist.txt") == false);

                // Cannot mount within a host tree
                REQUIRE(F.mount(CMAKE_SOURCE_DIR, "/build/test-folder") == FSResult::NotValidMount);
                {
                    REQUIRE(std::filesystem::is_directory(CMAKE_BINARY_DIR "/test-folder"));
                    REQUIRE(F.is_empty("/build/test-folder") == FSResult::True);
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


SCENARIO("get_type")
{
    FileSystem F;
    F.mkdir("/src");
    F.mkdir("/tmp");
    F.touch("/file.txt");

    F.mount(CMAKE_SOURCE_DIR, "/src");
    REQUIRE(F.get_type("/src/conanfile.py") == Type::HOST_FILE);
    REQUIRE(F.get_type("/src/include") == Type::HOST_DIR);

    REQUIRE(F.get_type("/file.txt") == Type::MEM_FILE);
    REQUIRE(F.get_type("/tmp") == Type::MEM_DIR);

    REQUIRE(F.get_type("/src") == Type::HOST_DIR);
}
SCENARIO("Opening Files")
{
    FileSystem F;
    F.mkdir("/src");
    F.mkdir("/bin");
    F.mount(CMAKE_SOURCE_DIR, "/src");
    F.mount(CMAKE_BINARY_DIR, "/bin");

    REQUIRE(F.touch("/test.txt") == FSResult::Success);

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
    THEN("We can use read on the stream")
    {
        auto f = F.open("/src/conanfile.py", std::ios::in | std::ios::binary);
        REQUIRE(f);

        std::vector<char> bytes(1024 * 1024);
        f.read(&bytes[0], 1024 * 1024);
        auto count = f.gcount();
        
        REQUIRE(count > 0);
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
    THEN("We can use read on the stream")
    {
        auto f = F.open("/test.txt", std::ios::in | std::ios::binary);
        REQUIRE(f);

        std::vector<char> bytes(1024 * 1024);
        f.read(&bytes[0], 1024 * 1024);
        auto count = f.gcount();

        REQUIRE(count > 0);
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

SCENARIO("Copying/Moving Files")
{
    FileSystem F;
    F.mkdir("/src");
    F.mkdir("/bin");
    F.mount(CMAKE_SOURCE_DIR, "/src");
    F.mount(CMAKE_BINARY_DIR, "/bin");

    WHEN("We copy from the host to mem")
    {
        REQUIRE(F.cp("/src/conanfile.py", "/conanfile.py") == FSResult::Success);

        THEN("The dst now exists")
        {
            REQUIRE( F.exists("/conanfile.py"));
        }
        THEN("The files contain the same data")
        {
            REQUIRE(F.file_to_string("/conanfile.py") == F.file_to_string("/src/conanfile.py"));
        }

        WHEN("We copy from the mem to host")
        {
            std::filesystem::remove(CMAKE_BINARY_DIR "/conanfile.py");
            REQUIRE(!std::filesystem::exists(CMAKE_BINARY_DIR "/conanfile.py" ));
            REQUIRE(F.cp("/conanfile.py", "/bin/conanfile.py") == FSResult::Success);

            THEN("The dst now exists")
            {
                REQUIRE(std::filesystem::exists(CMAKE_BINARY_DIR "/conanfile.py" ));
            }
            THEN("The files contain the same data")
            {
                REQUIRE(F.file_to_string("/bin/conanfile.py") == F.file_to_string("/src/conanfile.py"));
            }
        }

        WHEN("We move from mem to mem")
        {
            F.mv("/conanfile.py", "/conanfile2.py");
            REQUIRE(F.exists("/conanfile2.py"));
            REQUIRE(!F.exists("/conanfile.py"));
            REQUIRE(F.file_to_string("/conanfile2.py") == F.file_to_string("/src/conanfile.py"));
        }
        WHEN("We move from mem to host")
        {
            std::filesystem::remove(CMAKE_BINARY_DIR "/conanfile2.py");
            REQUIRE(!std::filesystem::exists(CMAKE_BINARY_DIR "/conanfile2.py" ));
            F.mv("/conanfile.py", "/bin/conanfile2.py");

#if 1
            THEN("the dst exists")
            {
                REQUIRE(F.exists("/bin/conanfile2.py"));
            }
            THEN("The src doesn't exist")
            {
                REQUIRE(!F.exists("/conanfile.py"));
            }
            THEN("The data contains the same")
            {
                REQUIRE(F.file_to_string("/src/conanfile.py") == F.file_to_string("/bin/conanfile2.py"));
            }
#endif
        }
    }



}

#endif
