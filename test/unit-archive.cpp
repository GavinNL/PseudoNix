#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <PseudoNix/System.h>
#include <PseudoNix/Shell.h>
#include <array>

#include <archive.h>
#include <archive_entry.h>

namespace PseudoNix
{
struct ArchiveNodeMount : public PseudoNix::MountHelper
{
    std::filesystem::path host_path;

    struct EntryInfo
    {
        bool is_dir = false;
    };

    std::map<std::filesystem::path, EntryInfo> _files;

    ArchiveNodeMount(std::filesystem::path const & hostPath) : host_path(hostPath)
    {
        namespace fs = std::filesystem;
        auto abs_path = host_path;

        struct archive *a;
        struct archive_entry *entry;
        int r;
        // Create archive reader
        a = archive_read_new();
        archive_read_support_format_tar(a); // Add TAR support
        archive_read_support_format_zip(a);
        // Open the archive file

        r = archive_read_open_filename(a, abs_path.c_str(), 10240); // 10KB block size
        if (r != ARCHIVE_OK) {
            fprintf(stderr, "Could not open archive: %s\n", archive_error_string(a));
        }

        // Loop through each entry
        while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
            const char *pathname = archive_entry_pathname(entry);
            //printf("%s\n", pathname);  // Print file or directory path
            archive_read_data_skip(a); // Skip file content (we only want the names)

            std::string path_s = pathname;
            fs::path pth = path_s;
            _clean(pth);
            EntryInfo e;
            e.is_dir = path_s.back() == '/';
            _files[pth] = e;
        }

        // Clean up
        archive_read_close(a);
        archive_read_free(a);
    }

    bool read_only = false;

    bool exists(std::filesystem::path const & path) const override
    {
        assert(!path.has_root_directory());
        auto it = _files.find(path);
        return it != _files.end();
    }

    bool remove(std::filesystem::path const & path) const  override
    {
        return false;
        assert(!path.has_root_directory());
        return std::filesystem::remove(host_path / path);
    }
    bool is_dir(std::filesystem::path const & path) const  override
    {
        auto it = _files.find(path);
        if(it == _files.end())
            return false;

        return it->second.is_dir;
    }
    bool is_file(std::filesystem::path const & path) const  override
    {
        auto it = _files.find(path);
        if(it == _files.end())
            return false;

        return !it->second.is_dir;
    }
    FSResult mkdir(std::filesystem::path const & path)  override
    {
        (void)path;
        return FSResult::ReadOnlyFileSystem;
    }
    FSResult touch(std::filesystem::path const & path)  override
    {
        (void)path;
        return FSResult::ReadOnlyFileSystem;
    }
    bool is_empty(std::filesystem::path const & path) const  override
    {
        namespace fs = std::filesystem;
        auto abs_path = host_path / path;
        return fs::is_empty(abs_path);
    }
    PseudoNix::generator<std::filesystem::path> list_dir(std::filesystem::path path) const  override
    {
        (void)path;
        for(auto & [pth, info] : _files)
        {
            co_yield pth;
        }
    }
};

}

int list_tar_contents(const char *filename) {
    struct archive *a;
    struct archive_entry *entry;
    int r;

    // Create archive reader
    a = archive_read_new();
    archive_read_support_format_tar(a); // Add TAR support
    archive_read_support_format_zip(a);
    // Open the archive file
    r = archive_read_open_filename(a, filename, 10240); // 10KB block size
    if (r != ARCHIVE_OK) {
        fprintf(stderr, "Could not open archive: %s\n", archive_error_string(a));
        return 1;
    }

    // Loop through each entry
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char *pathname = archive_entry_pathname(entry);
        printf("%s\n", pathname);  // Print file or directory path
        archive_read_data_skip(a); // Skip file content (we only want the names)
    }

    // Clean up
    archive_read_close(a);
    archive_read_free(a);

    return 0;
}

class ArchiveEntryStreamBuf : public std::streambuf {
    static constexpr std::size_t BUFFER_SIZE = 8192;
    struct archive* archive_;
    std::vector<char> buffer_;

public:
    ArchiveEntryStreamBuf(struct archive* archive)
        : archive_(archive), buffer_(BUFFER_SIZE) {
        setg(buffer_.data(), buffer_.data(), buffer_.data());
    }

protected:
    int underflow() override {
        ssize_t n = archive_read_data(archive_, buffer_.data(), buffer_.size());
        if (n <= 0) {
            return traits_type::eof();  // EOF or error
        }
        setg(buffer_.data(), buffer_.data(), buffer_.data() + n);
        return traits_type::to_int_type(*gptr());
    }
};

class ArchiveEntryStream : public std::istream {
    ArchiveEntryStreamBuf buf_;

public:
    ArchiveEntryStream(struct archive* archive)
        : std::istream(&buf_), buf_(archive) {}
};

SCENARIO("Test archive loading")
{
    const char* filename = CMAKE_SOURCE_DIR "/archive.tar";
    struct archive* a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);

    if (archive_read_open_filename(a, filename, 10240) != ARCHIVE_OK) {
        std::cerr << "Failed to open archive\n";
        return;
    }

    struct archive_entry* entry;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char* name = archive_entry_pathname(entry);
        std::cout << "Reading entry: " << name << "\n";

        // Wrap this entry in an istream
        ArchiveEntryStream entryStream(a);

        // Read a few lines or characters
        std::string line;
        while (std::getline(entryStream, line)) {
            std::cout << "  " << line << "\n";
        }

        std::cout << "--- End of " << name << " ---\n\n";
    }

    archive_read_close(a);
    archive_read_free(a);
}

using namespace PseudoNix;

SCENARIO("Mount")
{
    FileSystem F;

    GIVEN("A filesystem and a directory")
    {
        REQUIRE(F.exists("/"));
        REQUIRE(F.mkdir("/tar") == FSResult::Success );

        WHEN("We mount a host directory")
        {
            REQUIRE(F.mount_t<ArchiveNodeMount>(CMAKE_SOURCE_DIR "/archive.tar", "/tar") == FSResult::Success);

            for(auto f : F.list_dir("/tar"))
            {
                std::cout << f << F.is_dir(std::filesystem::path("/tar") / f) << std::endl;
            }

            REQUIRE(F.touch("/tar/file") == FSResult::ReadOnlyFileSystem);
            REQUIRE(F.mkdir("/tar/file") == FSResult::ReadOnlyFileSystem);

            REQUIRE(F.is_dir("/tar/folder"));
            REQUIRE(!F.is_file("/tar/folder"));

            REQUIRE(!F.is_dir("/tar/file.txt"));
            REQUIRE( F.is_file("/tar/file.txt"));

            REQUIRE(!F.is_dir("/tar/folder/another_file.txt"));
            REQUIRE( F.is_file("/tar/folder/another_file.txt"));

            REQUIRE(F.file_to_string("/tar/file.txt") == "");
        }
    }
}
