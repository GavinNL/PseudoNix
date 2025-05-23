#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

//#include <PseudoNix/System.h>
//#include <PseudoNix/Shell.h>
#include <array>

#include <PseudoNix/FileSystem.h>
#include <PseudoNix/ArchiveMount.h>
#include <PseudoNix/sample_archive.h>
#include <zlib.h>

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

void writeVectorToFile(const std::vector<uint8_t>& data, const std::string& filename) {
    std::ofstream out(filename, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Failed to open file for writing: " + filename);
    }

    out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!out) {
        throw std::runtime_error("Failed to write data to file: " + filename);
    }
}

std::string file_to_string(FileSystem & F, FileSystem::path_type path)
{
    auto in = F.openRead(path);
    std::stringstream buffer;
    buffer << in.rdbuf();        // read entire file into buffer
    return buffer.str();           // convert to string
}

std::vector<uint8_t> decompressGzipToTar(const std::vector<uint8_t>& gzipData) {
    constexpr size_t CHUNK_SIZE = 262144; // 256 KB
    std::vector<uint8_t> output;

    z_stream strm{};
    strm.next_in = const_cast<Bytef*>(gzipData.data());
    strm.avail_in = static_cast<uInt>(gzipData.size());

    // 16 + MAX_WBITS tells zlib to decode a gzip stream
    if (inflateInit2(&strm, 16 + MAX_WBITS) != Z_OK) {
        throw std::runtime_error("inflateInit2 failed");
    }

    uint8_t outBuffer[CHUNK_SIZE];

    int ret;
    do {
        strm.next_out = outBuffer;
        strm.avail_out = CHUNK_SIZE;

        ret = inflate(&strm, Z_NO_FLUSH);

        if (ret == Z_MEM_ERROR || ret == Z_DATA_ERROR || ret == Z_NEED_DICT) {
            inflateEnd(&strm);
            throw std::runtime_error("inflate failed during decompression");
        }

        size_t have = CHUNK_SIZE - strm.avail_out;
        output.insert(output.end(), outBuffer, outBuffer + have);

    } while (ret != Z_STREAM_END);

    inflateEnd(&strm);
    return output;
}

#define ARCHIVE_TAR_GZ_PATH CMAKE_BINARY_DIR "/archive.tar.gz"
#define ARCHIVE_TAR_PATH    CMAKE_BINARY_DIR "/archive.tar"


SCENARIO("Uncompress")
{
    // uncompress the ziped archive
    auto archive_tar = decompressGzipToTar(archive_tar_gz);

    writeVectorToFile(archive_tar_gz, ARCHIVE_TAR_GZ_PATH);
    writeVectorToFile(archive_tar,    ARCHIVE_TAR_PATH);

    std::vector<uint8_t> randomData(1024);

    auto p = std::make_shared<ArchiveMount>(randomData.data(), randomData.size());
    for(auto d : p->_files)
    {
        std::cout << d.first << std::endl;
    }
    std::cout << "---" << std::endl;
}


SCENARIO("Mounting From File")
{
    FileSystem F;

    GIVEN("A filesystem and a directory")
    {
        REQUIRE(F.exists("/"));
        REQUIRE(F.mkdir("/tar") == FSResult::True );

        WHEN("We mount a uncompressed tar")
        {
            REQUIRE(F.mount<ArchiveMount>("/tar", ARCHIVE_TAR_PATH) == FSResult::True);

            REQUIRE(F.mkfile("/tar/file") == FSResult::ErrorReadOnly);
            REQUIRE(F.mkdir("/tar/file")  == FSResult::ErrorReadOnly);

            REQUIRE(F.getType("/tar/folder") == NodeType::MountDir);
            REQUIRE(F.getType("/tar/file.txt") == NodeType::MountFile);
            REQUIRE(F.getType("/tar/folder/another_file.txt") == NodeType::MountFile);

            REQUIRE(file_to_string(F,"/tar/file.txt") == "Hello world\n");
            REQUIRE(file_to_string(F,"/tar/folder/another_file.txt") == "goodbye world\n");

            for(auto f : F.list_dir("/tar"))
            {
                std::cout << f << std::endl;
            }
        }
        WHEN("We mount a compressed tar")
        {
            REQUIRE(F.mount<ArchiveMount>("/tar", ARCHIVE_TAR_GZ_PATH) == FSResult::True);

            REQUIRE(F.mkfile("/tar/file") == FSResult::ErrorReadOnly);
            REQUIRE(F.mkdir("/tar/file")  == FSResult::ErrorReadOnly);

            REQUIRE(F.getType("/tar/folder") == NodeType::MountDir);
            REQUIRE(F.getType("/tar/file.txt") == NodeType::MountFile);
            REQUIRE(F.getType("/tar/folder/another_file.txt") == NodeType::MountFile);

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
    FileSystem F;

    GIVEN("A filesystem and a directory")
    {
        REQUIRE(F.exists("/"));
        REQUIRE(F.mkdir("/tar") == FSResult::True );

        WHEN("We mount a uncompressed tar")
        {
            auto raw_data = loadFile(ARCHIVE_TAR_PATH);
            REQUIRE(F.mount<ArchiveMount>("/tar", raw_data.data(), raw_data.size()) == FSResult::True);

            REQUIRE(F.mkfile("/tar/file") == FSResult::ErrorReadOnly);
            REQUIRE(F.mkdir("/tar/file")  == FSResult::ErrorReadOnly);

            REQUIRE(F.getType("/tar/folder") == NodeType::MountDir);
            REQUIRE(F.getType("/tar/file.txt") == NodeType::MountFile);
            REQUIRE(F.getType("/tar/folder/another_file.txt") == NodeType::MountFile);

            REQUIRE(file_to_string(F,"/tar/file.txt") == "Hello world\n");
            REQUIRE(file_to_string(F,"/tar/folder/another_file.txt") == "goodbye world\n");

            for(auto f : F.list_dir("/tar"))
            {
                std::cout << f << std::endl;
            }
        }
        WHEN("We mount a compressed tar")
        {
            auto raw_data = loadFile(ARCHIVE_TAR_GZ_PATH);
            REQUIRE(F.mount<ArchiveMount>("/tar", raw_data.data(), raw_data.size()) == FSResult::True);

            REQUIRE(F.mkfile("/tar/file") == FSResult::ErrorReadOnly);
            REQUIRE(F.mkdir("/tar/file")  == FSResult::ErrorReadOnly);

            REQUIRE(F.getType("/tar/folder") == NodeType::MountDir);
            REQUIRE(F.getType("/tar/file.txt") == NodeType::MountFile);
            REQUIRE(F.getType("/tar/folder/another_file.txt") == NodeType::MountFile);

            REQUIRE(file_to_string(F,"/tar/file.txt") == "Hello world\n");
            REQUIRE(file_to_string(F,"/tar/folder/another_file.txt") == "goodbye world\n");

            for(auto f : F.list_dir("/tar"))
            {
                std::cout << f << std::endl;
            }
        }
    }
}
