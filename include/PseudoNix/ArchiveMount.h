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

    bool open(void const* data, size_t length, std::filesystem::path sub)
    {
        open_archive(data, length);

        return seek_entry(sub);
    }

    bool open(std::filesystem::path host_path, std::filesystem::path sub)
    {
        open_archive(host_path);

        return seek_entry(sub);
    }
    bool open_archive(void const * data, size_t length)
    {
        // Create archive reader
        archive_ = archive_read_new();
        archive_read_support_format_tar(archive_); // Add TAR support
        archive_read_support_format_zip(archive_);

        int r = archive_read_open_memory2(archive_, data, length, 10240); // 10KB block size
        if (r != ARCHIVE_OK) {
            fprintf(stderr, "Could not open archive: %s\n", archive_error_string(archive_));
            return false;
        }

        return true;
    }
    bool open_archive(std::filesystem::path host_path)
    {
        // Create archive reader
        archive_ = archive_read_new();
        archive_read_support_format_tar(archive_); // Add TAR support
        archive_read_support_format_zip(archive_);

        int r = archive_read_open_filename(archive_, host_path.c_str(), 10240); // 10KB block size
        if (r != ARCHIVE_OK) {
            fprintf(stderr, "Could not open archive: %s\n", archive_error_string(archive_));
            return false;
        }

        return true;
    }

    std::filesystem::path next_entry()
    {
        struct archive_entry *entry;
        // Loop through each entry
        if(archive_read_next_header(archive_, &entry) == ARCHIVE_OK)
        {
            const char *pathname = archive_entry_pathname(entry);
            //archive_read_data_skip(archive_); // Skip file content (we only want the names)
            return pathname;
        }

        return {};
    }

    bool seek_entry(std::filesystem::path sub)
    {
        while(true)
        {
            auto p = next_entry();
            if(p.empty())
                return false;

            if(p == sub)
                break;
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
    void const * _data = nullptr;
    size_t _length = 0;
    struct EntryInfo
    {
        bool is_dir = false;
    };

    std::map<std::filesystem::path, EntryInfo> _files;

    ArchiveNodeMount(std::filesystem::path const & hostPath) : host_path(hostPath)
    {
        ArchiveEntryStreamBuf buf;
        buf.open_archive(hostPath);

        do
        {
            auto p = buf.next_entry();
            if(p.empty())
                break;

            std::string path_s = p.generic_string();
            std::filesystem::path pth = path_s;
            _clean(pth);
            EntryInfo e;
            e.is_dir = path_s.back() == '/';
            _files[pth] = e;

        } while(true);

    }

    ArchiveNodeMount(void const* data, size_t length)
    {
        _data = data;
        _length = length;

        ArchiveEntryStreamBuf buf;
        buf.open_archive(data, length);

        do
        {
            auto p = buf.next_entry();
            if(p.empty())
                break;

            std::string path_s = p.generic_string();
            std::filesystem::path pth = path_s;
            _clean(pth);
            EntryInfo e;
            e.is_dir = path_s.back() == '/';
            _files[pth] = e;

        } while(true);

    }

    bool exists(std::filesystem::path const & path) const override
    {
        if(path == ".")
            return true;
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
        if(path == ".")
            return true;
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
            if(path.lexically_relative(pth) == "..")
            {
                co_yield pth;
            }
        }
    }

    std::unique_ptr<std::streambuf> open(const std::string& path, std::ios::openmode mode) override
    {
        (void)mode;
        auto p = std::make_unique<ArchiveEntryStreamBuf>();
        if(!host_path.empty())
            p->open(host_path, path);
        else
            p->open(_data, _length, path);
        return p;
    }
};

}


#endif
