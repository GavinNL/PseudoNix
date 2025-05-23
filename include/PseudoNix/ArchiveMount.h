#ifndef PSEUDONIX_ARCHIVE_MOUNT2_H
#define PSEUDONIX_ARCHIVE_MOUNT2_H

#include <archive.h>
#include <archive_entry.h>
#include <map>
#include <vector>
#include <format>
#include "FileSystemMount.h"
#include "FileSystemHelpers.h"
#include "System.h"

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

struct ArchiveMount : public FSMountBase
{
    path_type host_path;
    void const * _data = nullptr;
    size_t _length = 0;
    std::string _info;
    struct EntryInfo
    {
        bool is_dir = false;
    };

    std::map<path_type, EntryInfo> _files;

    ArchiveMount(path_type const & hostPath) : host_path(hostPath)
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

    ArchiveMount(void const* data, size_t length)
        :ArchiveMount(data, length, std::format("{}", data))
    {

    }

    ArchiveMount(void const* data, size_t length, std::string info)
    {
        _data = data;
        _length = length;
        _info = info;
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

    virtual bool is_read_only() const override
    {
        return true;
    }

    std::string get_info() override
    {
        return _info;
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
        return result_type::ErrorReadOnly;
        (void)relPath;
    }
    virtual result_type mkfile(path_type relPath) override
    {
        return result_type::ErrorReadOnly;
        (void)relPath;
    }

    virtual NodeType getType(path_type relPath) const override
    {
        if(relPath == "." || relPath.empty())
            return NodeType::MountDir;

        auto it = _files.find(relPath);
        if(it == _files.end())
            return NodeType::NoExist;

        return it->second.is_dir ? NodeType::MountDir : NodeType::MountFile;
    }

    virtual result_type remove(path_type relPath) override
    {
        return result_type::ErrorReadOnly;
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

inline void enable_archive_mount(System & sys)
{
    sys.setFunction("archive", "Mount tar and tar.gz files", [](PseudoNix::System::e_type ctrl) -> PseudoNix::System::task_type
    {
        PSEUDONIX_PROC_START(ctrl);

        std::map<std::string, std::string> typeToMnt;

        // 0    1     2   3
        // host mount SRC DST
        //
        if(ARGS.size() == 4)
        {
            PseudoNix::System::path_type ACT  = ARGS[1];
            PseudoNix::System::path_type SRC  = ARGS[2];
            PseudoNix::System::path_type DST  = ARGS[3];

            if(ACT == "mount")
            {
                HANDLE_PATH(CWD, DST);
                HANDLE_PATH(CWD, SRC);

                if( SYSTEM.getType(SRC) == PseudoNix::NodeType::MemFile)
                {
                    auto p = SYSTEM.getVirtualFileData(SRC);
                    if(p)
                    {
                        auto er = SYSTEM.mount<PseudoNix::ArchiveMount>(DST, p->data(), p->size(), SRC.generic_string());

                        co_return er == PseudoNix::FSResult::True;
                    }
                }
                COUT << std::format("Archive {} does not exist in the VFS\n", SRC.generic_string());
            }
            co_return 0;
        }

        COUT << std::format("Unknown error\n");

        co_return 1;
    });
}

}


#endif
