#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <PseudoNix/System.h>
#include <PseudoNix/Shell.h>
#include <array>


#include <PseudoNix/ArchiveMount.h>

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

SCENARIO("Mounting From File")
{
    FileSystem F;

    GIVEN("A filesystem and a directory")
    {
        REQUIRE(F.exists("/"));
        REQUIRE(F.mkdir("/tar") == FSResult::Success );

        WHEN("We mount a uncompressed tar")
        {
            REQUIRE(F.mount2_t<ArchiveNodeMount>("/tar", CMAKE_BINARY_DIR "/archive.tar") == FSResult::Success);

            REQUIRE(F.touch("/tar/file") == FSResult::ReadOnlyFileSystem);
            REQUIRE(F.mkdir("/tar/file") == FSResult::ReadOnlyFileSystem);

            REQUIRE(F.is_dir("/tar/folder"));
            REQUIRE(!F.is_file("/tar/folder"));

            REQUIRE(!F.is_dir("/tar/file.txt"));
            REQUIRE( F.is_file("/tar/file.txt"));

            REQUIRE(!F.is_dir("/tar/folder/another_file.txt"));
            REQUIRE( F.is_file("/tar/folder/another_file.txt"));

            REQUIRE(F.file_to_string("/tar/file.txt") == "Hello world\n");
            REQUIRE(F.file_to_string("/tar/folder/another_file.txt") == "goodbye world\n");

            for(auto f : F.list_dir("/tar"))
            {
                std::cout << f << std::endl;
            }
        }

        WHEN("When we mount a zip compressed tar")
        {
            REQUIRE(F.mount2_t<ArchiveNodeMount>("/tar", CMAKE_BINARY_DIR "/archive.tar.gz") == FSResult::Success);

            REQUIRE(F.touch("/tar/file") == FSResult::ReadOnlyFileSystem);
            REQUIRE(F.mkdir("/tar/file") == FSResult::ReadOnlyFileSystem);

            REQUIRE(F.is_dir("/tar/folder"));
            REQUIRE(!F.is_file("/tar/folder"));

            REQUIRE(!F.is_dir("/tar/file.txt"));
            REQUIRE( F.is_file("/tar/file.txt"));

            REQUIRE(!F.is_dir("/tar/folder/another_file.txt"));
            REQUIRE( F.is_file("/tar/folder/another_file.txt"));

            REQUIRE(F.file_to_string("/tar/file.txt") == "Hello world\n");
            REQUIRE(F.file_to_string("/tar/folder/another_file.txt") == "goodbye world\n");

            for(auto f : F.list_dir("/tar"))
            {
                std::cout << f << std::endl;
            }
        }

    }
}

SCENARIO("Mounting From Memory")
{
    FileSystem F;

    GIVEN("A filesystem and a directory")
    {
        REQUIRE(F.exists("/"));
        REQUIRE(F.mkdir("/tar") == FSResult::Success );


#ifndef _WIN32
        WHEN("we mount an uncompressed tar from memory")
        {
            auto raw_data = loadFile(CMAKE_BINARY_DIR "/archive.tar");

            REQUIRE(F.mount2_t<ArchiveNodeMount>("/tar", raw_data.data(), raw_data.size()) == FSResult::Success);
            REQUIRE(F.exists("/tar"));
            REQUIRE(F.is_dir("/tar"));

            REQUIRE(F.touch("/tar/file") == FSResult::ReadOnlyFileSystem);
            REQUIRE(F.mkdir("/tar/file") == FSResult::ReadOnlyFileSystem);

            REQUIRE(F.is_dir("/tar/folder"));
            REQUIRE(!F.is_file("/tar/folder"));

            REQUIRE(!F.is_dir("/tar/file.txt"));
            REQUIRE( F.is_file("/tar/file.txt"));

            REQUIRE(!F.is_dir("/tar/folder/another_file.txt"));
            REQUIRE( F.is_file("/tar/folder/another_file.txt"));

            REQUIRE(F.file_to_string("/tar/file.txt") == "Hello world\n");
            REQUIRE(F.file_to_string("/tar/folder/another_file.txt") == "goodbye world\n");

            for(auto f : F.list_dir("/tar"))
            {
                std::cout << f << std::endl;
            }
        }
#endif
        WHEN("we mount an zip-compressed tar from memory")
        {
            auto raw_data = loadFile(CMAKE_BINARY_DIR "/archive.tar.gz");

            REQUIRE(F.mount2_t<ArchiveNodeMount>("/tar", raw_data.data(), raw_data.size()) == FSResult::Success);
            REQUIRE(F.exists("/tar"));
            REQUIRE(F.is_dir("/tar"));

            REQUIRE(F.touch("/tar/file") == FSResult::ReadOnlyFileSystem);
            REQUIRE(F.mkdir("/tar/file") == FSResult::ReadOnlyFileSystem);

            REQUIRE(F.is_dir("/tar/folder"));
            REQUIRE(!F.is_file("/tar/folder"));

            REQUIRE(!F.is_dir("/tar/file.txt"));
            REQUIRE( F.is_file("/tar/file.txt"));

            REQUIRE(!F.is_dir("/tar/folder/another_file.txt"));
            REQUIRE( F.is_file("/tar/folder/another_file.txt"));

            REQUIRE(F.file_to_string("/tar/file.txt") == "Hello world\n");
            REQUIRE(F.file_to_string("/tar/folder/another_file.txt") == "goodbye world\n");

            for(auto f : F.list_dir("/tar"))
            {
                std::cout << f << std::endl;
            }
        }
    }
}
