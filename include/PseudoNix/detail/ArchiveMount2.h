#ifndef PSEUDONIX_ARCHIVE_MOUNT2_H
#define PSEUDONIX_ARCHIVE_MOUNT2_H

#include <archive.h>
#include <archive_entry.h>
#include <map>
#include <vector>
#include "../FileSystemMount.h"
#include "../FileSystemHelpers.h"

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
        archive_read_support_filter_gzip(archive_);

        int r = archive_read_open_memory(archive_, data, length); // 10KB block size
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
        archive_read_support_filter_gzip(archive_);

        int r = archive_read_open_filename(archive_, host_path.generic_string().c_str(), 10240); // 10KB block size
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
        auto n = archive_read_data(archive_, buffer_.data(), buffer_.size());
        if (n <= 0) {
            return traits_type::eof();  // EOF or error
        }
        setg(buffer_.data(), buffer_.data(), buffer_.data() + n);
        return traits_type::to_int_type(*gptr());
    }

};

struct ArchiveNodeMount2 : public FSMountBase
{
    path_type host_path;
    void const * _data = nullptr;
    size_t _length = 0;
    struct EntryInfo
    {
        bool is_dir = false;
    };

    std::map<path_type, EntryInfo> _files;

    ArchiveNodeMount2(path_type const & hostPath) : host_path(hostPath)
    {
        ArchiveEntryStreamBuf buf;
        if(!buf.open_archive(hostPath))
        {
            return;
        }

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

    ArchiveNodeMount2(void const* data, size_t length)
    {
        _data = data;
        _length = length;

        ArchiveEntryStreamBuf buf;
        if(!buf.open_archive(data, length))
        {
            return;
        }

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

    result_type exists(path_type path) const override
    {
        if(path == ".")
            return result_type::True;
        assert(!path.has_root_directory());
        auto it = _files.find(path);
        return it != _files.end() ? result_type::True : result_type::False;
    }

    virtual result_type mkdir(path_type relPath) override
    {
        return result_type::False;
        (void)relPath;
    }
    virtual result_type mkfile(path_type relPath) override
    {
        return result_type::False;
        (void)relPath;
    }

    virtual NodeType2 getType(path_type relPath) const override
    {
        if(relPath == "." || relPath.empty())
            return NodeType2::MountDir;

        auto it = _files.find(relPath);
        if(it == _files.end())
            return NodeType2::NoExist;

        return it->second.is_dir ? NodeType2::MountDir : NodeType2::MountFile;
    }

    virtual result_type rm(path_type relPath) override
    {
        return result_type::False;
        (void)relPath;
    }



    PseudoNix::Generator<std::filesystem::path> list_dir(path_type path) override
    {
        (void)path;
        if(path == ".")
            path.clear();
        for(auto & [pth, info] : _files)
        {
            if(path.lexically_relative(pth) == "..")
            {
                co_yield pth.lexically_relative(path);
            }
        }
    }

    std::unique_ptr<std::streambuf> open(path_type relPath, std::ios::openmode mode) override
    {
        (void)mode;
        auto p = std::make_unique<ArchiveEntryStreamBuf>();
        if(!host_path.empty())
            p->open(host_path, relPath);
        else
            p->open(_data, _length, relPath);
        return p;
    }
};

}


#endif
