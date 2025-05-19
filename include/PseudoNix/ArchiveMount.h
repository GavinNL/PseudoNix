#ifndef PSEUDONIX_ARCHIVE_MOUNT_H
#define PSEUDONIX_ARCHIVE_MOUNT_H

#include "FileSystem.h"

#include <archive.h>
#include <archive_entry.h>

namespace PseudoNix
{

class ArchiveEntryStreamBuf : public std::streambuf {
    static constexpr std::size_t BUFFER_SIZE = 8192;
    struct archive* archive_ = nullptr;
    std::vector<char> buffer_;

public:
    ArchiveEntryStreamBuf() : buffer_(BUFFER_SIZE) {}

    ~ArchiveEntryStreamBuf()
    {
        if(archive_)
        {
            archive_read_close(archive_);
            archive_read_free(archive_);
        }
    }

    bool open(std::filesystem::path host_path, std::filesystem::path sub)
    {
        // Create archive reader
        struct archive_entry *entry;
        archive_ = archive_read_new();
        archive_read_support_format_tar(archive_); // Add TAR support
        archive_read_support_format_zip(archive_);

        // Open the archive file
        int r = archive_read_open_filename(archive_, host_path.c_str(), 10240); // 10KB block size
        if (r != ARCHIVE_OK) {
            fprintf(stderr, "Could not open archive: %s\n", archive_error_string(archive_));
            return false;
        }

        // Loop through each entry
        while (archive_read_next_header(archive_, &entry) == ARCHIVE_OK) {
            const char *pathname = archive_entry_pathname(entry);

            if(pathname == sub)
            {
                break;
            }
            //printf("%s\n", pathname);  // Print file or directory path
            archive_read_data_skip(archive_); // Skip file content (we only want the names)
        }

        setg(buffer_.data(), buffer_.data(), buffer_.data());
        return true;
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

    std::unique_ptr<std::streambuf> open(const std::string& path, std::ios::openmode mode) override
    {
        (void)mode;
        auto p = std::make_unique<ArchiveEntryStreamBuf>();
        p->open(host_path, path);
        return p;
    }
};

}


#endif
