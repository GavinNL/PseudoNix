#ifndef PSEUDONIX_FILESYSTEM2_HOST_MOUNT_H
#define PSEUDONIX_FILESYSTEM2_HOST_MOUNT_H

#include <string>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <format>
#include "FileSystemMount.h"
#include "System.h"

namespace PseudoNix
{

class DelegatingFileStreamBuf : public std::streambuf {
    static constexpr std::size_t buffer_size = 4096;

    FILE* file = nullptr;
    char input_buffer[buffer_size];
    char output_buffer[buffer_size];

public:
    DelegatingFileStreamBuf() {
        setg(input_buffer, input_buffer, input_buffer);
        setp(output_buffer, output_buffer + buffer_size);
    }

    ~DelegatingFileStreamBuf() {
        sync();
        if (file) {
            fclose(file);
        }
    }

    bool open(const std::filesystem::path& path, std::ios::openmode mode) {
        std::string cmode;

        bool read = (mode & std::ios::in);
        bool write = (mode & std::ios::out);
        bool append = (mode & std::ios::app);
        bool truncate = (mode & std::ios::trunc);

        if (read && write) {
            if (append)        cmode = "a+";
            else if (truncate) cmode = "w+";
            else               cmode = "r+";
        } else if (read) {
            cmode = "r";
        } else if (write) {
            if (append)        cmode = "a";
            //else if (truncate) cmode = "w";
            else               cmode = "w";  // Default to truncating if only writing
        } else {
            return false; // Invalid mode
        }

        // Always open in binary mode
        cmode += "b";

        file = nullptr;
        auto pstr = path.generic_string();
#ifdef _WIN32
        errno_t err = fopen_s(&file, pstr.c_str(), cmode.c_str());
        bool success = (err == 0 && file != nullptr);
#else
        file = std::fopen(pstr.c_str(), cmode.c_str());
        bool success = (file != nullptr);
#endif
        return success;
    }

    void close() {
        sync(); // flush output buffer
        if (file) {
            std::fclose(file);
            file = nullptr;
        }
    }

protected:
    // Read from file into input buffer
    int_type underflow() override {
        if (!file || feof(file)) return traits_type::eof();

        std::size_t n = std::fread(input_buffer, 1, buffer_size, file);
        if (n == 0) return traits_type::eof();

        setg(input_buffer, input_buffer, input_buffer + n);
        return traits_type::to_int_type(*gptr());
    }

    // Flush the output buffer to the file
    int_type overflow(int_type ch) override {
        if (!file) return traits_type::eof();

        auto n = pptr() - pbase();
        if (n > 0) {
            std::fwrite(pbase(), 1, static_cast<size_t>(n), file);
        }
        setp(output_buffer, output_buffer + buffer_size);

        if (ch != traits_type::eof()) {
            *pptr() = static_cast<char_type>(ch);
            pbump(1);
        }

        return traits_type::not_eof(ch);
    }

    // Flush on sync (e.g., std::endl)
    int sync() override {
        return overflow(traits_type::eof()) == traits_type::eof() ? -1 : 0;
    }
};


/**
 * @brief The FSNodeHostMount class
 *
 * A node for mounting host directories
 */
struct FSNodeHostMount : public FSMountBase
{
    std::filesystem::path m_path_on_host;
    FSNodeHostMount(std::filesystem::path path_on_host) : m_path_on_host(path_on_host)
    {
    }

    virtual NodeType getType(path_type relPath) const override
    {
        if( std::filesystem::is_directory(m_path_on_host / relPath) )
        {
            return NodeType::MountDir;
        }
        if( std::filesystem::is_regular_file(m_path_on_host / relPath) )
        {
            return NodeType::MountFile;
        }
        return NodeType::NoExist;
    }

    virtual result_type exists(path_type relPath) const override
    {
        auto b = std::filesystem::exists( m_path_on_host / relPath );
        return b ? result_type::True : result_type::False;
    }

    virtual result_type remove(path_type relPath) override
    {
        (void)relPath;
        return std::filesystem::remove(m_path_on_host / relPath) ? result_type::True : result_type::False;
    }

    virtual result_type mkdir(path_type relPath) override
    {
        (void)relPath;
        return std::filesystem::create_directory(m_path_on_host / relPath) ? result_type::True : result_type::False;
    }

    virtual result_type mkfile(path_type relPath) override
    {
        (void)relPath;
        std::ofstream out(m_path_on_host / relPath);
        if(out)
        {
            out.close();
            return result_type::True;
        }
        return result_type::False;
    }

    virtual std::unique_ptr<std::streambuf> open(path_type relPath, std::ios::openmode mode) override
    {
        (void)mode;
        auto p = std::make_unique<DelegatingFileStreamBuf>();
        p->open(m_path_on_host / relPath, mode);
        return p;
    }

    virtual Generator<path_type> list_dir(path_type relPath) override
    {
        namespace fs = std::filesystem;
        auto abs_path = m_path_on_host / relPath;
        for (const auto& entry : fs::directory_iterator(abs_path)) {
            co_yield entry.path().lexically_proximate(abs_path);
        }
    }

    virtual bool is_read_only() const override
    {
        return false;
    }

    std::string get_info() override
    {
        return std::format("host://{}", m_path_on_host.generic_string());
    }
};

inline void enable_host_mount(System & sys)
{
    sys.setFunction("host", "Mount host file systems", [](PseudoNix::System::e_type ctrl) -> PseudoNix::System::task_type
                    {
                        PSEUDONIX_PROC_START(ctrl);

                        std::map<std::string, std::string> typeToMnt;

                        // 0    1     2   3
                        // host mount SRC DST
                        //
                        if(ARGS.size() == 4)
                        {
                            System::path_type ACT  = ARGS[1];
                            System::path_type SRC  = ARGS[2];
                            System::path_type DST  = ARGS[3];

                            if(ACT == "mount")
                            {
                                HANDLE_PATH(CWD, DST);
                                HANDLE_PATH(CWD, SRC);

                                if( !std::filesystem::is_directory(SRC))
                                {
                                    COUT << std::format("Directory {} does not exist on the host\n", SRC.generic_string());
                                    co_return 1;
                                }
                                auto er = SYSTEM.mount<FSNodeHostMount>(DST, SRC);
                                FS_PRINT_ERROR(er);
                            }
                            co_return 0;
                        }

                        COUT << std::format("Unknown error\n");

                        co_return 1;
                    });
}

using HostMount = FSNodeHostMount;

}

#endif
