#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <PseudoNix/FileSystem.h>
#include <PseudoNix/HostMount.h>

using namespace PseudoNix;

std::string StreamToString(std::istream & in)
{
    std::stringstream buffer;
    buffer << in.rdbuf();        // read entire file into buffer
    auto str = buffer.str();           // convert to string
    while (str.ends_with('\n') || str.ends_with('\r'))
        str.pop_back();
    return str;
}
std::string readFile(const std::string& filename) {
    std::ifstream file(filename);  // open file
    if (!file) throw std::runtime_error("Could not open file");

    std::stringstream buffer;
    buffer << file.rdbuf();        // read entire file into buffer
    auto str = buffer.str();           // convert to string

    while(str.ends_with('\n') || str.ends_with('\r'))
        str.pop_back();
    return str;
}

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
    FileSystem F;

    REQUIRE(F.exists("/") == FSResult::True);

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

    REQUIRE(F.exists("/hello") == FSResult::True);
    REQUIRE(F.exists("/hello2") == FSResult::False);
}

SCENARIO("Exists")
{
    FileSystem F;

    REQUIRE(F.exists("/") == FSResult::True);
    REQUIRE(F.exists("/hello") == FSResult::False);
}

SCENARIO("mkdir")
{
    GIVEN("A an empty filesystem")
    {
        FileSystem F;
        REQUIRE(F.exists("/") == FSResult::True);

        THEN("We can check if folders dont exist")
        {
            REQUIRE(F.exists("/bye") == FSResult::False);
        }
        THEN("We cannot create mulitple directories")
        {
            REQUIRE(F.mkdir("/hello/world") == FSResult::ErrorParentDoesNotExist);
        }
        THEN("We can create a directory")
        {
            REQUIRE(F.exists("/hello") == FSResult::False);
            REQUIRE(F.mkdir("/hello") == FSResult::True);
            REQUIRE(F.exists("/hello") == FSResult::True);

            THEN("We can create a sub directory")
            {
                REQUIRE(F.mkdir("/hello/world") == FSResult::True);
                REQUIRE(F.exists("/hello/world") == FSResult::True);
            }
        }
    }
}


SCENARIO("mkfile")
{
    GIVEN("A an empty filesystem")
    {
        FileSystem F;
        REQUIRE(F.exists("/") == FSResult::True);

        THEN("We can create a file in the root folder")
        {
            REQUIRE(F.exists("/hello") == FSResult::False);
            REQUIRE(F.mkfile("/hello") == FSResult::True);
            REQUIRE(F.exists("/hello") == FSResult::True);

            THEN("We cannot create the file again")
            {
                REQUIRE(F.mkfile("/hello") == FSResult::ErrorExists);
            }
            THEN("We can create a file in a subfolder")
            {
                REQUIRE(F.mkdir("/hello2") == FSResult::True);
                REQUIRE(F.mkdir("/hello2/world") == FSResult::True);

                REQUIRE(F.mkfile("/hello2/world/file") == FSResult::True);

                REQUIRE(F.exists("/hello2/world/file") == FSResult::True);
            }
        }
    }
}

SCENARIO("rm")
{
    GIVEN("A filesystem with some directories and files")
    {
        FileSystem F;
        REQUIRE(F.exists("/") == FSResult::True);

        THEN("We can remove a single directory")
        {
            REQUIRE(F.mkdir("/bin"));
            REQUIRE(F.remove("/bin") == FSResult::True);
            REQUIRE(F.exists("/bin") == FSResult::False);
        }

        THEN("We cannot delete non-empty folders")
        {
            REQUIRE(F.mkdir("/etc"));
            REQUIRE(F.mkfile("/etc/profile.txt"));
            REQUIRE(F.remove("/etc") == FSResult::ErrorNotEmpty);

            WHEN("We make the folder empty")
            {
                REQUIRE(F.remove("/etc/profile.txt") == FSResult::True);
                REQUIRE(F.exists("/etc/profile.txt") == FSResult::False);

                THEN("We can delete the folder")
                {
                    REQUIRE(F.remove("/etc") == FSResult::True);
                    REQUIRE(F.exists("/etc") == FSResult::False);
                }
            }
        }
    }
}


SCENARIO("mount")
{
    GIVEN("A filesystem with some directories and files")
    {
        FileSystem F;

        THEN("We can mount a directory")
        {
            REQUIRE(F.exists("/") == FSResult::True);
            REQUIRE(F.mkdir("/src") == FSResult::True);
            REQUIRE( NodeType::MemDir == F.getType("/src"));
            REQUIRE( FSResult::True == F.mount<FSNodeHostMount>("/src", CMAKE_SOURCE_DIR));

            REQUIRE(F.exists("/src/conanfile.py") == FSResult::True);
            REQUIRE(F.exists("/src/noexist.py") == FSResult::False);

            REQUIRE(F.exists("/src/test") == FSResult::True);
            REQUIRE(F.exists("/src/test/CMakeLists.txt") == FSResult::True);

            REQUIRE(FSResult::True == F.mkfile("/hello.txt"));

            THEN("We can check the types")
            {
                REQUIRE( NodeType::MemDir == F.getType("/"));
                REQUIRE( NodeType::MemFile == F.getType("/hello.txt"));
                REQUIRE( NodeType::MountDir == F.getType("/src"));
                REQUIRE( NodeType::MountFile == F.getType("/src/conanfile.py"));
                REQUIRE( NodeType::MountDir == F.getType("/src/test"));
                REQUIRE( NodeType::NoExist == F.getType("/src/test/fasdfasdf"));
                REQUIRE( NodeType::NoExist == F.getType("/bye.txt"));
            }
            THEN("We can unmount a directory")
            {
                REQUIRE( FSResult::True == F.unmount("/src"));

                REQUIRE(F.exists("/src/conanfile.py") == FSResult::False);
                REQUIRE(F.exists("/src/noexist.py")   == FSResult::False);

                REQUIRE(F.exists("/src/test") == FSResult::False);
                REQUIRE(F.exists("/src/test/CMakeLists.txt") == FSResult::False);
            }
        }
    }
}



SCENARIO("ref")
{
    GIVEN("A filesystem with some directories and files")
    {
        FileSystem F;

        THEN("We can mount a directory")
        {
            REQUIRE(F.exists("/") == FSResult::True);
            REQUIRE(F.mkdir("/src") == FSResult::True);
            REQUIRE(F.mkfile("/file.txt") == FSResult::True);

            REQUIRE( NodeType::MemDir == F.getType("/src"));
            REQUIRE( FSResult::True == F.mount<FSNodeHostMount>("/src", CMAKE_SOURCE_DIR));

            // check mounted file
            REQUIRE(true  == F.fs("/src/conanfile.py").exists());
            REQUIRE(true  == F.fs("/src/conanfile.py").is_file());
            REQUIRE(false == F.fs("/src/conanfile.py").is_dir());
            REQUIRE(false == F.fs("/src/conanfile.py").is_mount_point());
            REQUIRE(true  == F.fs("/src/conanfile.py").is_mounted());

            // check mounted dir
            REQUIRE(true  == F.fs("/src/test").exists());
            REQUIRE(false == F.fs("/src/test").is_file());
            REQUIRE(true  == F.fs("/src/test").is_dir());
            REQUIRE(false == F.fs("/src/test").is_mount_point());
            REQUIRE(true  == F.fs("/src/test").is_mounted());

            // check mount point
            REQUIRE(true  == F.fs("/src").exists());
            REQUIRE(false == F.fs("/src").is_file());
            REQUIRE(true  == F.fs("/src").is_dir());
            REQUIRE(true  == F.fs("/src").is_mount_point());
            REQUIRE(true  == F.fs("/src").is_mounted());

            // check virtual dir
            REQUIRE(true  == F.fs("/").exists());
            REQUIRE(false == F.fs("/").is_file());
            REQUIRE(true  == F.fs("/").is_dir());
            REQUIRE(false == F.fs("/").is_mount_point());
            REQUIRE(false == F.fs("/").is_mounted());

            // check virtual file
            REQUIRE(true  == F.fs("/file.txt").exists());
            REQUIRE(true  == F.fs("/file.txt").is_file());
            REQUIRE(false == F.fs("/file.txt").is_dir());
            REQUIRE(false == F.fs("/file.txt").is_mount_point());
            REQUIRE(false == F.fs("/file.txt").is_mounted());
        }
    }
}

SCENARIO("mount with file manipulation")
{
    GIVEN("A filesystem with some directories and files")
    {
        FileSystem F;

        REQUIRE(F.exists("/") == FSResult::True);
        REQUIRE(F.mkdir("/build") == FSResult::True);
        REQUIRE( FSResult::True == F.mount<FSNodeHostMount>("/build", CMAKE_BINARY_DIR));
        REQUIRE(F.exists("/build/CMakeCache.txt") == FSResult::True);

        std::filesystem::remove_all(CMAKE_BINARY_DIR "/test-folder");

        THEN("Create a directory on the host through the VFS mount")
        {
            REQUIRE(FSResult::True == F.mkdir("/build/test-folder"));
            REQUIRE(std::filesystem::exists(CMAKE_BINARY_DIR "/test-folder"));
            REQUIRE(FSResult::True == F.exists("/build/test-folder"));

            THEN("Create a file on the host through the VFS mount")
            {
                REQUIRE(FSResult::True == F.mkfile("/build/test-folder/test.file"));
                REQUIRE(std::filesystem::exists(CMAKE_BINARY_DIR "/test-folder/test.file"));
                REQUIRE(FSResult::True == F.exists("/build/test-folder/test.file"));
            }
        }
        std::filesystem::remove_all(CMAKE_BINARY_DIR "/test-folder");
    }
}

SCENARIO("write to files")
{
    GIVEN("A filesystem with some directories and files")
    {
        FileSystem F;
        REQUIRE(F.exists("/") == FSResult::True);
        REQUIRE(F.mkdir("/folder") == FSResult::True);
        REQUIRE(F.mkfile("/folder/file.txt") == FSResult::True);

        {
            auto out = F.openWrite("/folder/file.txt", false);
            out << "Hello";
        }

        {
            std::string str;
            auto in = F.openRead("/folder/file.txt");
            in >> str;
            REQUIRE(str == "Hello");
        }

        {
            std::string str;
            auto in = F.openRead("/folder/file.txt");
            in >> str;
            REQUIRE(str == "Hello");
        }

        {
            auto out = F.openWrite("/folder/file.txt", true);
            out << " World";
        }

        {
            std::string str;
            auto in = F.openRead("/folder/file.txt");
            std::getline(in, str);
            REQUIRE(str == "Hello World");
        }
    }
}


SCENARIO("Remove files/directories")
{
    GIVEN("A filesystem with some directories and files")
    {
        FileSystem F;
        REQUIRE(F.exists("/") == FSResult::True);
        REQUIRE(F.mkdir("/folder") == FSResult::True);
        REQUIRE(F.mkdir("/folder/A") == FSResult::True);
        REQUIRE(F.mkfile("/folder/B") == FSResult::True);
        REQUIRE(F.mkfile("/C") == FSResult::True);
        REQUIRE(F.mkdir("/D") == FSResult::True);

        REQUIRE(F.exists("/folder") == FSResult::True);
        REQUIRE(F.exists("/folder/A") == FSResult::True);
        REQUIRE(F.exists("/folder/B") == FSResult::True);
        REQUIRE(F.exists("/C") == FSResult::True);
        REQUIRE(F.exists("/D") == FSResult::True);


        // not empty
        REQUIRE(F.remove("/folder") == FSResult::ErrorNotEmpty);
        REQUIRE(F.remove("/folder/A") == FSResult::True);
        REQUIRE(F.exists("/folder/A") == FSResult::False);

        REQUIRE(F.remove("/folder/B") == FSResult::True);
        REQUIRE(F.exists("/folder/B") == FSResult::False);

        REQUIRE(F.remove("/C") == FSResult::True);
        REQUIRE(F.remove("/D") == FSResult::True);
        REQUIRE(F.exists("/C") == FSResult::False);
        REQUIRE(F.exists("/D") == FSResult::False);

        REQUIRE(F.remove("/C/does/not/exist") == FSResult::ErrorDoesNotExist);
    }
}



SCENARIO("Remove files/directories on host")
{
    GIVEN("A filesystem with some directories and files")
    {
        FileSystem F;
        REQUIRE(F.exists("/") == FSResult::True);

        std::filesystem::remove_all(CMAKE_BINARY_DIR "/folder");
        std::filesystem::create_directories(CMAKE_BINARY_DIR "/folder");

        REQUIRE(F.mkdir("/folder") == FSResult::True);
        REQUIRE( FSResult::True == F.mount<FSNodeHostMount>("/folder", CMAKE_BINARY_DIR "/folder"));

        REQUIRE(F.mkdir("/folder/A") == FSResult::True);
        REQUIRE(F.mkfile("/folder/B") == FSResult::True);
        REQUIRE(F.mkfile("/C") == FSResult::True);
        REQUIRE(F.mkdir("/D") == FSResult::True);

        REQUIRE(F.exists("/folder") == FSResult::True);
        REQUIRE(F.exists("/folder/A") == FSResult::True);
        REQUIRE(F.exists("/folder/B") == FSResult::True);
        REQUIRE(F.exists("/C") == FSResult::True);
        REQUIRE(F.exists("/D") == FSResult::True);


        // not empty
        REQUIRE(F.remove("/folder") == FSResult::ErrorReadOnly);
        REQUIRE(F.remove("/folder/A") == FSResult::True);
        REQUIRE(F.exists("/folder/A") == FSResult::False);

        REQUIRE(F.remove("/folder/B") == FSResult::True);
        REQUIRE(F.exists("/folder/B") == FSResult::False);

        REQUIRE(F.remove("/C") == FSResult::True);
        REQUIRE(F.remove("/D") == FSResult::True);
        REQUIRE(F.exists("/C") == FSResult::False);
        REQUIRE(F.exists("/D") == FSResult::False);

        REQUIRE(F.remove("/C/does/not/exist") == FSResult::ErrorDoesNotExist);
    }
}


SCENARIO("Copying file from Mem->Mem")
{
    GIVEN("A filesystem with some directories and files")
    {
        FileSystem F;
        REQUIRE(F.exists("/") == FSResult::True);
        REQUIRE(F.mkfile("/file.txt") == FSResult::True);
        REQUIRE(F.mkdir("/folder") == FSResult::True);
        {
            auto out = F.openWrite("/file.txt", false);
            out << "Hello";
        }
        REQUIRE(FSResult::True == F.exists("/file.txt") );

        THEN("We can copy from memfile to memfile")
        {
            REQUIRE(FSResult::True  == F.copy("/file.txt", "/dst.txt") );
            REQUIRE(FSResult::True  == F.exists("/dst.txt") );
            REQUIRE(FSResult::True == F.exists("/file.txt") );

            {
                std::string str = F.fs("/dst.txt");//.openRead("/dst.txt");
                REQUIRE(str == "Hello");
            }
        }
        THEN("We can copy from memfile to mem folder")
        {
            REQUIRE(FSResult::True  == F.copy("/file.txt", "/folder") );
            REQUIRE(FSResult::True  == F.exists("/folder/file.txt") );
            REQUIRE(FSResult::True  == F.exists("/file.txt") );

            {
                std::string str;
                auto in = F.openRead("/folder/file.txt");
                in >> str;
                REQUIRE(str == "Hello");
            }
        }
    }
}

SCENARIO("Copying file from Mount->Mem->Mount")
{
    GIVEN("A filesystem with some directories and files")
    {
        FileSystem F;
        REQUIRE(F.exists("/") == FSResult::True);
        REQUIRE(F.mkdir("/src") == FSResult::True);
        REQUIRE(F.mkdir("/dst") == FSResult::True);
        REQUIRE(F.mount<FSNodeHostMount>("/src", CMAKE_SOURCE_DIR "/archive") == FSResult::True);
        REQUIRE(F.mount<FSNodeHostMount>("/dst", CMAKE_BINARY_DIR) == FSResult::True);

        std::filesystem::remove_all(CMAKE_BINARY_DIR "/file.txt");

        auto ACTUAL_DATA = readFile(CMAKE_SOURCE_DIR "/archive/file.txt");
        REQUIRE(ACTUAL_DATA.size() > 0);
        REQUIRE(ACTUAL_DATA == "Hello world");
        REQUIRE(FSResult::True == F.exists("/src/file.txt") );

        THEN("We can copy from mount file to mem file")
        {
            REQUIRE(FSResult::True  == F.copy("/src/file.txt", "/dst.txt") );
            REQUIRE(FSResult::True  == F.exists("/src/file.txt") );
            REQUIRE(std::filesystem::exists(CMAKE_SOURCE_DIR "/archive/file.txt"));

            REQUIRE(FSResult::True  == F.exists("/dst.txt") );

            {
                auto in = F.openRead("/dst.txt");
                auto str = StreamToString(in);
                REQUIRE(str == ACTUAL_DATA);
            }

            THEN("We can copy from mem file to mount file")
            {
                REQUIRE(FSResult::True  == F.copy("/dst.txt", "/dst/dst.txt") );
                REQUIRE(FSResult::True  == F.exists("/dst.txt") );
                REQUIRE(std::filesystem::exists(CMAKE_BINARY_DIR "/dst.txt"));

                REQUIRE(FSResult::True  == F.exists("/dst/dst.txt") );

                {
                    auto in = F.openRead("/dst/dst.txt");
                    auto str = StreamToString(in);
                    REQUIRE(str == ACTUAL_DATA);
                }

                THEN("We can copy from mount file to mem file")
                {
                    REQUIRE(FSResult::True  == F.copy("/dst/dst.txt", "/dst2.txt") );
                    REQUIRE(FSResult::True  == F.exists("/dst2.txt") );
                    REQUIRE(std::filesystem::exists(CMAKE_BINARY_DIR "/dst.txt"));

                    REQUIRE(FSResult::True  == F.exists("/dst/dst.txt") );

                    {
                        auto in = F.openRead("/dst2.txt");
                        auto str = StreamToString(in);
                        REQUIRE(str == ACTUAL_DATA);
                    }
                }
            }
        }
    }
}

SCENARIO("Moving file from Mem->Mem")
{
    GIVEN("A filesystem with some directories and files")
    {
        FileSystem F;
        REQUIRE(F.exists("/") == FSResult::True);
        REQUIRE(F.mkfile("/file.txt") == FSResult::True);
        REQUIRE(F.mkdir("/folder") == FSResult::True);
        {
            auto out = F.openWrite("/file.txt", false);
            out << "Hello";
        }
        REQUIRE(FSResult::True == F.exists("/file.txt") );

        THEN("We can copy from memfile to memfile")
        {
            REQUIRE(FSResult::True  == F.move("/file.txt", "/dst.txt") );
            REQUIRE(FSResult::True  == F.exists("/dst.txt") );
            REQUIRE(FSResult::False == F.exists("/file.txt") );

            {
                std::string str;
                auto in = F.openRead("/dst.txt");
                in >> str;
                REQUIRE(str == "Hello");
            }
        }
        THEN("We can copy from memfile to mem folder")
        {
            REQUIRE(FSResult::True  == F.move("/file.txt", "/folder") );
            REQUIRE(FSResult::True  == F.exists("/folder/file.txt") );
            REQUIRE(FSResult::False == F.exists("/file.txt") );

            {
                std::string str;
                auto in = F.openRead("/folder/file.txt");
                in >> str;
                REQUIRE(str == "Hello");
            }
        }
    }
}

SCENARIO("Moving file from Mount->Mem->Mount")
{
    GIVEN("A filesystem with some directories and files")
    {
        FileSystem F;
        REQUIRE(F.exists("/") == FSResult::True);
        REQUIRE(F.mkdir("/dst") == FSResult::True);
        REQUIRE(F.mount<FSNodeHostMount>("/dst", CMAKE_BINARY_DIR) == FSResult::True);

        std::filesystem::remove_all(CMAKE_BINARY_DIR "/A.txt");
        REQUIRE(!std::filesystem::exists(CMAKE_BINARY_DIR "/A.txt"));
        std::filesystem::copy_file(CMAKE_SOURCE_DIR "/archive/file.txt", CMAKE_BINARY_DIR "/A.txt");
        REQUIRE(std::filesystem::exists(CMAKE_BINARY_DIR "/A.txt"));

        auto ACTUAL_DATA = readFile(CMAKE_BINARY_DIR "/A.txt");
        REQUIRE(ACTUAL_DATA.size() > 0);

        THEN("We can move from Mount to Mount")
        {
            REQUIRE(FSResult::True  == F.move("/dst/A.txt", "/dst/B.txt") );
            REQUIRE(std::filesystem::exists(CMAKE_BINARY_DIR "/B.txt"));
            REQUIRE(!std::filesystem::exists(CMAKE_BINARY_DIR "/A.txt"));
            REQUIRE(FSResult::True  == F.exists("/dst/B.txt") );
            REQUIRE(FSResult::False == F.exists("/dst/A.txt") );

            {
                auto in = F.openRead("/dst/B.txt");
                auto str = StreamToString(in);
                REQUIRE(str == ACTUAL_DATA);
            }

            THEN("We can move from Mount to Mem")
            {
                REQUIRE(FSResult::True  == F.move("/dst/B.txt", "/C.txt") );
                REQUIRE(!std::filesystem::exists(CMAKE_BINARY_DIR "/B.txt"));

                REQUIRE(FSResult::False  == F.exists("/dst/B.txt") );
                REQUIRE(FSResult::True == F.exists("/C.txt") );

                {
                    auto in = F.openRead("/C.txt");
                    auto str = StreamToString(in);
                    REQUIRE(str == ACTUAL_DATA);
                }


                THEN("We can move from Mem to Mount")
                {
                    REQUIRE(FSResult::True  == F.move("/C.txt", "/dst/D.txt") );
                    REQUIRE(std::filesystem::exists(CMAKE_BINARY_DIR "/D.txt"));

                    REQUIRE(FSResult::False  == F.exists("/C.txt") );
                    REQUIRE(FSResult::True == F.exists("/dst/D.txt") );

                    {
                        auto in = F.openRead("/dst/D.txt");
                        auto str = StreamToString(in);
                        REQUIRE(str == ACTUAL_DATA);
                    }
                }
            }
        }
    }
}

SCENARIO("Moving Folders from Mem->Mem")
{
    GIVEN("A filesystem with some directories and files")
    {
        FileSystem F;
        REQUIRE(F.exists("/")     == FSResult::True);
        REQUIRE(F.mkdir("/src")   == FSResult::True);
        REQUIRE(F.mkdir("/src/A") == FSResult::True);
        REQUIRE(F.mkdir("/src/B") == FSResult::True);
        REQUIRE(F.mkdir("/src/C") == FSResult::True);


        REQUIRE(F.getType("/src")   == NodeType::MemDir);
        REQUIRE(F.getType("/src/A") == NodeType::MemDir);
        REQUIRE(F.getType("/src/B") == NodeType::MemDir);
        REQUIRE(F.getType("/src/C") == NodeType::MemDir);

        THEN("We can move the folder")
        {
            REQUIRE(FSResult::True == F.move("/src", "/dst") );

            REQUIRE(F.getType("/dst")   == NodeType::MemDir);
            REQUIRE(F.getType("/dst/A") == NodeType::MemDir);
            REQUIRE(F.getType("/dst/B") == NodeType::MemDir);
            REQUIRE(F.getType("/dst/C") == NodeType::MemDir);

            REQUIRE(F.getType("/src")   == NodeType::NoExist);
            REQUIRE(F.getType("/src/A") == NodeType::NoExist);
            REQUIRE(F.getType("/src/B") == NodeType::NoExist);
            REQUIRE(F.getType("/src/C") == NodeType::NoExist);
        }
    }
}


std::set<std::filesystem::path> getAllFilePathsRecursively(const std::filesystem::path& root) {
    std::set<std::filesystem::path> filePaths;
    namespace fs = std::filesystem;
    if (!fs::exists(root) || !fs::is_directory(root)) {
        return filePaths; // Return empty set if root doesn't exist or isn't a directory
    }

    for (const auto& entry : fs::recursive_directory_iterator(root)) {
        //if (fs::is_regular_file(entry.path())) {
            filePaths.insert(fs::absolute(entry.path()).lexically_proximate(root));
            //filePaths.insert(entry.path());
        //}
    }

    return filePaths;
}

std::set<std::filesystem::path> getAllFilePaths(const std::filesystem::path& root) {
    std::set<std::filesystem::path> filePaths;
    namespace fs = std::filesystem;
    if (!fs::exists(root) || !fs::is_directory(root)) {
        return filePaths; // Return empty set if root doesn't exist or isn't a directory
    }

    for (const auto& entry : fs::directory_iterator(root)) {
        //if (fs::is_regular_file(entry.path())) {
        filePaths.insert(fs::absolute(entry.path()).lexically_proximate(root));
        //filePaths.insert(entry.path());
        //}
    }

    return filePaths;
}

SCENARIO("List Dir")
{
    GIVEN("A filesystem with some directories and files")
    {
        FileSystem F;
        REQUIRE(F.exists("/")     == FSResult::True);
        REQUIRE(F.mkdir("/src")   == FSResult::True);
        REQUIRE(F.mkdir("/src/A") == FSResult::True);
        REQUIRE(F.mkdir("/src/B") == FSResult::True);
        REQUIRE(F.mkdir("/src/C") == FSResult::True);
        REQUIRE(F.mkdir("/src/C/sub") == FSResult::True);

        REQUIRE(F.mkdir("/build") == FSResult::True);

        std::filesystem::path host_dir = CMAKE_SOURCE_DIR "/archive";

        REQUIRE(F.mount<FSNodeHostMount>("/build", host_dir) == FSResult::True);

        std::set<FileSystem::path_type> valid_vir = {
            "A", "B", "C"
        };

        auto valid_host = getAllFilePaths(host_dir);

        std::set<FileSystem::path_type> valid_vir_rec = {
            "A", "B", "C", "C/sub"
        };

        std::set<FileSystem::path_type> valid_host_rec = getAllFilePathsRecursively(host_dir);

        std::set<FileSystem::path_type> valid_all = {"src", "build"};
        for(auto & v : valid_vir_rec)
        {
            valid_all.insert("src" / v);
        }
        for(auto & v : valid_host_rec)
        {
            valid_all.insert("build" / v);
        }

        THEN("We can list all files in a virtual folder")
        {
            auto & valid = valid_vir;
            for(auto n : F.list_dir("/src"))
            {
                std::cout << n << std::endl;
                REQUIRE(valid.count(n) == 1);
                valid.erase(n);
            }
            REQUIRE(valid.size() == 0);
        }

        THEN("We can list all files in a mounted folder")
        {
            auto & valid = valid_host;
            for(auto n : F.list_dir("/build"))
            {
                std::cout << n << std::endl;
                REQUIRE(valid.count(n) == 1);
                valid.erase(n);
            }
            REQUIRE(valid.size() == 0);
        }

        THEN("We can list all files in a virtual folder recursively")
        {
            std::cout << "----" << std::endl;
            auto & valid = valid_vir_rec;
            for(auto n : F.list_dir_recursive("/src"))
            {
                std::cout << n << std::endl;
                REQUIRE(valid.count(n) == 1);
                valid.erase(n);
            }
            REQUIRE(valid.size() == 0);
        }

        THEN("We can list all files in a mount folder recursively")
        {
            auto & valid = valid_host_rec;
            std::cout << "----" << std::endl;

            for(auto n : F.list_dir_recursive("/build"))
            {
                std::cout << n << std::endl;
                REQUIRE(valid.count(n) == 1);
                valid.erase(n);
            }
            REQUIRE(valid.size() == 0);
        }

        THEN("We can list all files in a combined vir/mount")
        {
            auto & valid = valid_all;
            std::cout << "----" << std::endl;

            for(auto n : F.list_dir_recursive("/"))
            {
                std::cout << n << std::endl;
                REQUIRE(valid.count(n) == 1);
                valid.erase(n);
            }
            REQUIRE(valid.size() == 0);
        }
    }
}


SCENARIO("Test ref") {

    GIVEN("A path that does not exist, but its parent is a file")
    {
        FileSystem F;
        F.mkdir("/folder");
        F.mkfile("/folder/file.txt");

        auto folder = F.fs("/folder");
        auto file_txt = F.fs("/folder/file.txt");

        auto ref = F.fs("/folder/file.txt/sub");

        THEN("The following are valid")
        {
            REQUIRE(ref.is_file()        == false);
            REQUIRE(ref.is_dir()         == false);
            REQUIRE(ref.is_mount_point() == false);
            REQUIRE(ref.is_mounted()     == false);
            REQUIRE(ref.dir_node()       == nullptr);
            REQUIRE(ref.file_node()      == file_txt.file_node());
            REQUIRE(ref.get_type()       == NodeType::NoExist);
            REQUIRE(ref.file_node()      == file_txt.file_node());
            REQUIRE(ref.exists()         == false);
        }
    }
    GIVEN("A path that does not exist, but its parent is a folder")
    {
        FileSystem F;
        F.mkdir("/folder");
        F.mkdir("/folder/file.txt");

        auto folder = F.fs("/folder");
        auto file_txt = F.fs("/folder/file.txt");

        auto ref = F.fs("/folder/file.txt/sub");

        THEN("The following are valid")
        {
            REQUIRE(ref.is_file()        == false);
            REQUIRE(ref.is_dir()         == false);
            REQUIRE(ref.is_mount_point() == false);
            REQUIRE(ref.is_mounted()     == false);
            REQUIRE(ref.dir_node()       == file_txt.dir_node());
            REQUIRE(ref.file_node()      == nullptr);
            REQUIRE(ref.get_type()       == NodeType::NoExist);
            REQUIRE(ref.file_node()      == file_txt.file_node());
            REQUIRE(ref.exists()         == false);
        }
    }
    GIVEN("A path that does not exist, but its parent is a mount")
    {
        FileSystem F;
        F.mkdir("/folder");
        F.mkdir("/folder/file.txt");
        REQUIRE(F.mount<FSNodeHostMount>("/folder/file.txt", CMAKE_SOURCE_DIR "/archive")== FSResult::True);

        auto folder = F.fs("/folder");
        auto file_txt = F.fs("/folder/file.txt");

        auto ref = F.fs("/folder/file.txt/sub");

        THEN("The following are valid")
        {
            REQUIRE(ref.is_mounted()     == true);

            REQUIRE(ref.is_file()        == false);
            REQUIRE(ref.is_dir()         == false);
            REQUIRE(ref.is_mount_point() == false);
            REQUIRE(ref.dir_node()       == file_txt.dir_node());
            REQUIRE(ref.file_node()      == nullptr);
            REQUIRE(ref.get_type()       == NodeType::NoExist);
            REQUIRE(ref.file_node()      == file_txt.file_node());
            REQUIRE(ref.exists()         == false);
        }
    }

}

SCENARIO("Test helpers")
{
    GIVEN("A filesystem with some directories and files")
    {
        FileSystem F;
        F.mkfile("/file.txt");
        F.fs("/file.txt") << std::string("hello");

        std::string h;
        F.fs("/file.txt") >> h;
        REQUIRE(h == "hello");
    }
}


SCENARIO("Test Read-Only")
{
    GIVEN("A filesystem with some directories and files")
    {
        FileSystem F;
        REQUIRE(F.mount<FSNodeHostMount>("/", CMAKE_BINARY_DIR)== FSResult::True);

        REQUIRE(F.mkfile("/file.txt") == FSResult::True);
        REQUIRE(F.is_read_only("/") == FSResult::False);
        REQUIRE(F.is_read_only("/file.txt") == FSResult::False);
        F.set_read_only("/", true);

        REQUIRE(F.is_read_only("/file.txt") == FSResult::True);
        REQUIRE(F.copy("/file.txt", "/file2.txt") == FSResult::ErrorReadOnly);
        REQUIRE(F.move("/file.txt", "/file2.txt") == FSResult::ErrorReadOnly);
        REQUIRE(F.remove("/file.txt") == FSResult::ErrorReadOnly);

        REQUIRE(FSResult::ErrorIsMounted == F.set_read_only("/file.txt", true));
    }
}

SCENARIO("Test mkdirs")
{
    GIVEN("A filesystem")
    {
        FileSystem F;

        WHEN("we call mkdirs") {

            REQUIRE(FSResult::True == F.mkdirs("/path/to/folder") );
            THEN("Expectation")
            {
                REQUIRE(F.exists("/path"));
                REQUIRE(F.exists("/path/to"));
                REQUIRE(F.exists("/path/to/folder"));
            }
        }

        WHEN("we call mkdirs with a file that exists") {
            REQUIRE(FSResult::True == F.mkfile("/path") );
            REQUIRE(FSResult::False == F.mkdirs("/path/to/folder") );
            THEN("Expectation")
            {
                REQUIRE(F.exists("/path"));
                REQUIRE(!F.exists("/path/to"));
                REQUIRE(!F.exists("/path/to/folder"));
            }
        }
    }
}


SCENARIO("Test Emplace Custom")
{
    GIVEN("A filesystem")
    {
        FileSystem F;

        struct Test
        {
            int x;
            double y;
        };

        WHEN("we call emplace_custom")
        {
            REQUIRE(FSResult::True == F.mkcustom<Test>("/path/to/myfile", Test{3, 3.0}));

            THEN("All subfolders exist")
            {
                REQUIRE(F.exists("/path"));
                REQUIRE(F.exists("/path/to"));
                REQUIRE(F.exists("/path/to/myfile"));
                REQUIRE(F.getType("/path/to/myfile") == NodeType::Custom);
            }
            THEN("We can get a pointer to the data")
            {
                auto x = F.getCustom<Test>("/path/to/myfile");
                REQUIRE(nullptr != x);
                REQUIRE(x->x == 3);
            }
            THEN("We cannot get another type")
            {
                REQUIRE(nullptr == F.getCustom<int>("/path/to/myfile"));
            }
        }
        WHEN("We emplace another type")
        {
            REQUIRE(FSResult::True == F.mkcustom<Test>("/path/to/myfile", Test{3, 3.0}));

            REQUIRE(FSResult::ErrorExists == F.mkcustom<int>("/path/to/myfile"));
        }
    }
}


