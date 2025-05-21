#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

//#include <PseudoNix/System.h>
//#include <PseudoNix/Shell.h>
#include <array>

#include <PseudoNix/detail/FileSystem2.h>
#include <PseudoNix/detail/ArchiveMount2.h>

using namespace PseudoNix;

std::vector<uint8_t> loadFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open file: " + filename);
    }

    // Seek to end to determine file size
    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    // Allocate vector and read contents
    std::vector<uint8_t> buffer( static_cast<size_t>(size) );
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        throw std::runtime_error("Failed to read file: " + filename);
    }

    return buffer;
}

std::string file_to_string(FileSystem2 & F, FileSystem2::path_type path)
{
    auto in = F.open(path, std::ios::in);
    std::stringstream buffer;
    buffer << in.rdbuf();        // read entire file into buffer
    return buffer.str();           // convert to string
}

SCENARIO("Mounting From File")
{
    FileSystem2 F;

    GIVEN("A filesystem and a directory")
    {
        REQUIRE(F.exists("/"));
        REQUIRE(F.mkdir("/tar") == FSResult2::True );

        WHEN("We mount a uncompressed tar")
        {
            REQUIRE(F.mount<ArchiveNodeMount2>("/tar", CMAKE_BINARY_DIR "/archive.tar") == FSResult2::True);

            REQUIRE(F.mkfile("/tar/file") == FSResult2::False);
            REQUIRE(F.mkdir("/tar/file")  == FSResult2::False);

            REQUIRE(F.getType("/tar/folder") == NodeType2::MountDir);
            REQUIRE(F.getType("/tar/file.txt") == NodeType2::MountFile);
            REQUIRE(F.getType("/tar/folder/another_file.txt") == NodeType2::MountFile);

            REQUIRE(file_to_string(F,"/tar/file.txt") == "Hello world\n");
            REQUIRE(file_to_string(F,"/tar/folder/another_file.txt") == "goodbye world\n");

            for(auto f : F.list_dir("/tar"))
            {
                std::cout << f << std::endl;
            }
        }
        WHEN("We mount a compressed tar")
        {
            REQUIRE(F.mount<ArchiveNodeMount2>("/tar", CMAKE_BINARY_DIR "/archive.tar.gz") == FSResult2::True);

            REQUIRE(F.mkfile("/tar/file") == FSResult2::False);
            REQUIRE(F.mkdir("/tar/file")  == FSResult2::False);

            REQUIRE(F.getType("/tar/folder") == NodeType2::MountDir);
            REQUIRE(F.getType("/tar/file.txt") == NodeType2::MountFile);
            REQUIRE(F.getType("/tar/folder/another_file.txt") == NodeType2::MountFile);

            REQUIRE(file_to_string(F,"/tar/file.txt") == "Hello world\n");
            REQUIRE(file_to_string(F,"/tar/folder/another_file.txt") == "goodbye world\n");

            for(auto f : F.list_dir("/tar"))
            {
                std::cout << f << std::endl;
            }
        }
    }
}

SCENARIO("Mounting From Memory")
{
    FileSystem2 F;

    GIVEN("A filesystem and a directory")
    {
        REQUIRE(F.exists("/"));
        REQUIRE(F.mkdir("/tar") == FSResult2::True );

        WHEN("We mount a uncompressed tar")
        {
            auto raw_data = loadFile(CMAKE_BINARY_DIR "/archive.tar");
            REQUIRE(F.mount<ArchiveNodeMount2>("/tar", raw_data.data(), raw_data.size()) == FSResult2::True);

            REQUIRE(F.mkfile("/tar/file") == FSResult2::False);
            REQUIRE(F.mkdir("/tar/file")  == FSResult2::False);

            REQUIRE(F.getType("/tar/folder") == NodeType2::MountDir);
            REQUIRE(F.getType("/tar/file.txt") == NodeType2::MountFile);
            REQUIRE(F.getType("/tar/folder/another_file.txt") == NodeType2::MountFile);

            REQUIRE(file_to_string(F,"/tar/file.txt") == "Hello world\n");
            REQUIRE(file_to_string(F,"/tar/folder/another_file.txt") == "goodbye world\n");

            for(auto f : F.list_dir("/tar"))
            {
                std::cout << f << std::endl;
            }
        }
        WHEN("We mount a compressed tar")
        {
            auto raw_data = loadFile(CMAKE_BINARY_DIR "/archive.tar.gz");
            REQUIRE(F.mount<ArchiveNodeMount2>("/tar", raw_data.data(), raw_data.size()) == FSResult2::True);

            REQUIRE(F.mkfile("/tar/file") == FSResult2::False);
            REQUIRE(F.mkdir("/tar/file")  == FSResult2::False);

            REQUIRE(F.getType("/tar/folder") == NodeType2::MountDir);
            REQUIRE(F.getType("/tar/file.txt") == NodeType2::MountFile);
            REQUIRE(F.getType("/tar/folder/another_file.txt") == NodeType2::MountFile);

            REQUIRE(file_to_string(F,"/tar/file.txt") == "Hello world\n");
            REQUIRE(file_to_string(F,"/tar/folder/another_file.txt") == "goodbye world\n");

            for(auto f : F.list_dir("/tar"))
            {
                std::cout << f << std::endl;
            }
        }
    }
}
