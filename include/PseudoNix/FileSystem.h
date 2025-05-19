#ifndef PSEUDONIX_FILESYSTEM_H
#define PSEUDONIX_FILESYSTEM_H

#include <format>
#include <map>
#include <string>
#include <typeindex>
#include <variant>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <any>
#include <vector>
#include "generator.h"

namespace PseudoNix
{

template<typename T>
using generator = Generator<T>;

class vector_backed_streambuf : public std::streambuf {
public:
    explicit vector_backed_streambuf(std::vector<char>& buffer)
        : buffer_(buffer)
    {
        setg(buffer_.data(), buffer_.data(), buffer_.data() + buffer_.size());
        setp(buffer_.data(), buffer_.data() + buffer_.size());
    }

    // Ensure output expansion if writing past current capacity
    int_type overflow(int_type ch) override {
        if (ch == traits_type::eof()) {
            return traits_type::not_eof(ch);
        }

        std::ptrdiff_t write_pos = pptr() - pbase();
        buffer_.push_back(static_cast<char>(ch));
        buffer_.resize(buffer_.size()); // Ensure capacity is correct

        char* base = buffer_.data();
        setp(base, base + buffer_.size());
        pbump(static_cast<int>(write_pos + 1));

        return ch;
    }

    // Called when input buffer is exhausted
    int_type underflow() override {
        if (gptr() >= egptr()) return traits_type::eof();
        return traits_type::to_int_type(*gptr());
    }

    // Sync not needed in memory buffer, but we can update get area
    int sync() override {
        std::ptrdiff_t size = pptr() - pbase();
        buffer_.resize(static_cast<size_t>(size));
        setg(buffer_.data(), buffer_.data(), buffer_.data() + buffer_.size());
        return 0;
    }

private:
    std::vector<char>& buffer_;
};

class string_backed_iostream : public std::iostream {
public:
    explicit string_backed_iostream(std::vector<char> & backing)
        : std::iostream(nullptr), buf(backing)
    {
        rdbuf(&buf);
    }

private:
    vector_backed_streambuf buf;
};


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

    bool open(const std::string& path, std::ios::openmode mode) {
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

        file = std::fopen(path.c_str(), cmode.c_str());
        return file != nullptr;
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

class FileStream : public std::iostream {
public:
    FileStream() : std::iostream(nullptr)
    {
    }

    // String-based constructor (backed by stringstream)
    explicit FileStream(std::vector<char> &initialContent)
        : std::iostream(nullptr)
    {
        backing = std::make_unique<string_backed_iostream>(initialContent);
        this->rdbuf(std::get<0>(backing)->rdbuf());
    }

    // File-based constructor (backed by fstream)
    explicit FileStream(const std::filesystem::path& filePath, std::ios::openmode mode)
        : std::iostream(nullptr), backing(std::fstream(filePath, mode)) {


        this->rdbuf(std::get<std::fstream>(backing).rdbuf());
    }

    explicit FileStream(std::unique_ptr<std::streambuf> && stream_buff) : std::iostream(nullptr)
    {
        this->rdbuf(stream_buff.get());
        _streamBuf = std::move(stream_buff);
    }
private:
    std::variant<std::unique_ptr<string_backed_iostream>, std::fstream> backing;
    std::unique_ptr<std::streambuf> _streamBuf;
};

enum class FSResult
{
    Success,
    True,
    False,
    PathExists,
    DoesNotExist,
    NotEmpty,
    NotValidMount,
    NotValidPath,
    NotADirectory,
    ReadOnlyFileSystem,
    HostDoesNotExist,
    CannotCreate,
    InvalidFileName,
    UnknownError
};

enum class Type
{
    DOES_NOT_EXIST,
    MEM_FILE,
    MEM_DIR,
    MOUNT,
    HOST_FILE,
    HOST_DIR,
    CUSTOM,
    UNKNOWN
};

struct NodeFile
{
    std::vector<char> filedata;
};

struct NodeCustom
{
    // use for storing any type of data
    std::any data;

    template<typename T>
    T& as()
    {
        return std::any_cast<T&>(data);
    }
    template<typename T>
    T const& as() const
    {
        return std::any_cast<T const&>(data);
    }
    template<typename T>
    bool is() const
    {
        return data.type() == std::type_index(typeid(T));
    }
};

struct NodeDir
{

};

struct MountHelper
{
    virtual ~MountHelper(){}

    virtual bool exists(std::filesystem::path const & path) const = 0;

    virtual bool remove(std::filesystem::path const & path) const = 0;

    virtual bool is_dir(std::filesystem::path const & path) const = 0;

    virtual bool is_file(std::filesystem::path const & path) const = 0;

    virtual FSResult mkdir(std::filesystem::path const & path) = 0;

    virtual FSResult touch(std::filesystem::path const & path) = 0;

    virtual bool is_empty(std::filesystem::path const & path) const = 0;

    virtual generator<std::filesystem::path> list_dir(std::filesystem::path path) const = 0;
};

struct FSNodeMount : public MountHelper
{
    std::filesystem::path host_path;

    FSNodeMount(std::filesystem::path const & hostPath) : host_path(hostPath)
    {
    }

    bool read_only = false;

    bool exists(std::filesystem::path const & path) const override
    {
        assert(!path.has_root_directory());
        return std::filesystem::exists(host_path / path);
    }

    bool remove(std::filesystem::path const & path) const  override
    {
        assert(!path.has_root_directory());
        return std::filesystem::remove(host_path / path);
    }
    bool is_dir(std::filesystem::path const & path) const  override
    {
        assert(!path.has_root_directory());
        return std::filesystem::is_directory(host_path / path);
    }
    bool is_file(std::filesystem::path const & path) const  override
    {
        assert(!path.has_root_directory());
        return std::filesystem::is_regular_file(host_path / path);
    }
    FSResult mkdir(std::filesystem::path const & path)  override
    {
        assert(!path.has_root_directory());
        if(read_only)
            return FSResult::ReadOnlyFileSystem;
        if(std::filesystem::create_directories(host_path / path))
        {
            return FSResult::Success;
        }
        return FSResult::CannotCreate;
    }
    FSResult touch(std::filesystem::path const & path)  override
    {
        assert(!path.has_root_directory());

        if(read_only)
            return FSResult::ReadOnlyFileSystem;;
        std::ofstream out(host_path/path);
        out.close();
        return FSResult::Success;
    }
    bool is_empty(std::filesystem::path const & path) const  override
    {
        namespace fs = std::filesystem;
        auto abs_path = host_path / path;
        return fs::is_empty(abs_path);
    }
    generator<std::filesystem::path> list_dir(std::filesystem::path path) const  override
    {
        namespace fs = std::filesystem;
        auto abs_path = host_path / path;
        for (const auto& entry : fs::directory_iterator(abs_path)) {
            co_yield entry.path().lexically_proximate(abs_path);
        }
    }
};

struct NodeMount
{
    std::filesystem::path host_path;
    bool read_only = false;
    std::unique_ptr<MountHelper> helper;

    bool exists(std::filesystem::path const & path) const
    {
        return helper->exists(path);
    }

    template<typename T>
    bool _is(std::filesystem::path const &p) const
    {
        if constexpr( std::is_same_v<T, NodeDir> )
        {
            return helper->is_dir(p);
        }
        if constexpr( std::is_same_v<T, NodeFile> )
        {
            return helper->is_file(p);
        }
        if constexpr( std::is_same_v<T, NodeCustom> )
        {
            return false;
        }
    }

    template<typename T>
    FSResult _mk(std::filesystem::path const &p)
    {
        if constexpr( std::is_same_v<T, NodeDir> )
        {
            return helper->mkdir(p);
        }
        if constexpr( std::is_same_v<T, NodeFile> )
        {
            return helper->touch(p);
        }
        if constexpr( std::is_same_v<T, NodeCustom> )
        {
            return FSResult::CannotCreate;
        }
    }
    bool remove(std::filesystem::path const & path) const
    {
        assert(!path.has_root_directory());
        return helper->remove(path);
    }
    bool is_dir(std::filesystem::path const & path) const
    {
        assert(!path.has_root_directory());
        return helper->is_dir(path);
        return std::filesystem::is_directory(host_path / path);
    }
    bool is_file(std::filesystem::path const & path) const
    {
        assert(!path.has_root_directory());
        return helper->is_file(path);
        return std::filesystem::is_regular_file(host_path / path);
    }
    FSResult mkdir(std::filesystem::path const & path)
    {
        assert(!path.has_root_directory());
        if(read_only)
            return FSResult::ReadOnlyFileSystem;
        return helper->mkdir(path);
        if(std::filesystem::create_directories(host_path / path))
        {
            return FSResult::Success;
        }
        return FSResult::CannotCreate;
    }
    FSResult touch(std::filesystem::path const & path)
    {
        assert(!path.has_root_directory());

        if(read_only)
            return FSResult::ReadOnlyFileSystem;;
        return helper->touch(path);
        std::ofstream out(host_path/path);
        out.close();
        return FSResult::Success;
    }
    bool is_empty(std::filesystem::path const & path) const
    {
        return helper->is_empty(path);
        namespace fs = std::filesystem;
        auto abs_path = host_path / path;
        return fs::is_empty(abs_path);
    }

    generator<std::filesystem::path> list_dir(std::filesystem::path path) const
    {
        for(auto xx : helper->list_dir(path))
        {
            co_yield xx;
        }
    }
};

using Node = std::variant<NodeDir, NodeFile, NodeCustom, NodeMount>;

inline void _clean(std::filesystem::path & P1)
{
    auto& p = P1;

    auto str = P1.generic_string();
    for (auto& s : str)
    {
        if (s == '\\') s = '/';
    }
    P1 = std::filesystem::path(str);

    if(p.has_root_directory())
    {
        P1 = std::filesystem::path("/") / p.lexically_normal().relative_path();
    }
    else
    {
        P1 = p.lexically_normal().relative_path();
    }


    if (P1.filename().empty())
    {
        P1 = p.parent_path();
    }
}

struct NodeRef
{
    Node * n;
    std::filesystem::path path;
};

struct FileSystem
{
    using node_type = Node;
    using path_type = std::filesystem::path;

    std::map<path_type, node_type> m_nodes;

    FileSystem()
    {
        m_nodes["/"] = NodeDir{ };
    }

    auto find_node(path_type path)
    {
        _clean(path);
        return m_nodes.find(path);
    }
    auto find_node(path_type path) const
    {
        _clean(path);
        return m_nodes.find(path);
    }
    /**
     * @brief exists
     * @param path
     * @return
     *
     * Checks if a path exists
     */
    bool exists(path_type path) const
    {
        path = path.lexically_normal();
        _clean(path);
        assert(path.has_root_directory());

        path_type root;
        for(auto const & p : path)
        {
            root /= p;
            auto it = find_node(root);
            if(it != m_nodes.end())
            {
                // found
                if(std::holds_alternative<NodeMount>(it->second))
                {
                    return std::get<NodeMount>(it->second).exists(path.lexically_relative(root));
                }
            }
            else
            {
                return false;
            }
        }
        return true;
    }


    template<typename T>
    bool _is(path_type p)
    {
        _clean(p);
        auto [it, sub] = find_parent_mount_split_it(p);
        if (it != m_nodes.end())
        {
            if (std::holds_alternative<NodeMount>(it->second))
                return std::get<NodeMount>(it->second)._is<T>(sub);
            return std::holds_alternative<T>(it->second);
        }
        return false;
    }

    bool is_dir(path_type p)
    {
        return _is<NodeDir>(p);
    }

    bool is_file(path_type p)
    {
        return _is<NodeFile>(p);
    }

    bool is_custom_file(path_type p)
    {
        return _is<NodeCustom>(p);
    }

    /**
     * @brief list_dir
     * @param path
     * @return
     *
     * Returns a generator which lists all files/directores
     * in the given paths
     */
    generator<path_type> list_dir(path_type path) const
    {
        _clean(path);
        auto [it, sub] = find_parent_mount_split_it(path);

        //auto it = find_node(path);
        if(it != m_nodes.end())
        {
            if(std::holds_alternative<NodeDir>(it->second))
            {
                auto it2 = std::next(it);
                while(it2 != m_nodes.end())
                {
                    if(path.lexically_relative(it2->first) == "..")
                    {
                        co_yield it2->first;
                    }
                    ++it2;
                }
                co_return;
            }
            if( std::holds_alternative<NodeMount>(it->second) && !sub.empty())
            {
                auto ll = std::get<NodeMount>(it->second).list_dir( sub );
                for(auto D : ll)
                {
                    co_yield it->first / D;
                }

            }
        }
        co_return;
    }

    /**
     * @brief mkdir
     * @param path
     * @return
     *
     * Creates a directory
     */
    FSResult mkdir(path_type path)
    {
        return _mk<NodeDir>(path, true);
    }

    /**
     * @brief mkcustom
     * @param path
     * @return
     *
     * Creates a custom file
     */
    FSResult mkcustom(path_type path)
    {
        return _mk<NodeCustom>(path, false);
    }

    /**
     * @brief touch
     * @param path
     * @return
     *
     * Creates an empty file
     */
    FSResult touch(path_type path)
    {
        return _mk<NodeFile>(path, false);
    }

    /**
     * @brief is_empty
     * @param path
     * @return
     *
     * Check if a directory is empty
     */
    FSResult is_empty(path_type path) const
    {
        _clean(path);
        auto [it, sub] = find_parent_mount_split_it(path);

        if(!sub.empty())
        {
            if(std::get<NodeMount>(it->second).is_empty( sub ))
                return FSResult::True;
            return FSResult::False;
        }
        else
        {
            if(it != m_nodes.end())
            {
                auto next = std::next(it);
                if(next == m_nodes.end())
                    return FSResult::True;

                if(!std::holds_alternative<NodeDir>(it->second))
                {
                    return FSResult::NotADirectory;
                }
                if(it->first.lexically_relative(next->first) == "..")
                {
                    return FSResult::False;
                }
                return FSResult::True;
            }
            else
            {
                return FSResult::DoesNotExist;
            }
        }
        return FSResult::UnknownError;
    }

    /**
     * @brief mount
     * @param host_path
     * @param path
     * @return
     *
     * Mounts a host directory into the virtual filesystem
     */
    FSResult mount( path_type host_path, path_type path)
    {
        if( !std::filesystem::is_directory(host_path))
        {
            return FSResult::HostDoesNotExist;
        }
        _clean(path);

        auto [it, sub] = find_parent_mount_split_it(path);

        // that path already exists inside
        // a mount point
        if(!sub.empty())
            return FSResult::NotValidMount;

        if(is_empty(path) != FSResult::True)
            return FSResult::NotEmpty;

        if( std::holds_alternative<NodeDir>(it->second))
        {
            it->second = NodeMount{host_path, false, std::make_unique<FSNodeMount>(host_path)};
            return FSResult::Success;
        }
        return FSResult::NotValidMount;
    }

    template<typename T>
    FSResult mount_t( path_type host_path, path_type path)
    {
        _clean(path);

        auto [it, sub] = find_parent_mount_split_it(path);

        // that path already exists inside
        // a mount point
        if(!sub.empty())
            return FSResult::NotValidMount;

        if(is_empty(path) != FSResult::True)
            return FSResult::NotEmpty;

        if( std::holds_alternative<NodeDir>(it->second))
        {
            it->second = NodeMount{host_path, false, std::make_unique<T>(host_path)};
            return FSResult::Success;
        }
        return FSResult::NotValidMount;
    }
    /**
     * @brief umount
     * @param path
     * @return
     *
     * Unmounts the host directory
     */
    FSResult umount(path_type path)
    {
        auto [it, sub] = find_parent_mount_split_it(path);
        //auto mnt = find_parent_mount(path);
        if(it != m_nodes.end() && sub.empty())
        {
            if(std::holds_alternative<NodeMount>(it->second))
            {
                it->second = NodeDir{};
                return FSResult::Success;
            }
        }

        return FSResult::NotValidMount;
    }

    /**
     * @brief cp
     * @param src
     * @param dst
     * @return
     *
     * Copies a single FILE to a destination.
     * the src file must exist
     *
     * dst can be a file or a directory
     */
    FSResult cp(path_type src, path_type dst)
    {
        _clean(src);
        _clean(dst);
        auto dst_type = get_type(dst);

        if(dst_type == Type::MEM_DIR || dst_type == Type::HOST_DIR)
        {
            touch(dst / src.filename());
            return copy(src, dst / src.filename());
        }
        else if(dst_type == Type::HOST_FILE || dst_type == Type::MEM_FILE)
        {
            return copy(src, dst);
        }
        else if(dst_type == Type::UNKNOWN)
        {
            if(exists(dst.parent_path()))
            {
                touch(dst);
                return copy(src, dst);
            }
            return FSResult::NotValidPath;
        }

        return FSResult::UnknownError;
    }

    /**
     * @brief mv
     * @param src
     * @param dst
     * @return
     *
     * Moves a single FILE from src to dst. src MUST exist
     *
     * Dst can be a file or a directory
     */
    FSResult mv(path_type const & src, path_type const & dst)
    {
        auto dst_type = get_type(dst);

        if(dst_type == Type::MEM_DIR || dst_type == Type::HOST_DIR)
        {
            touch(dst / src.filename());
            return move(src, dst / src.filename());
        }
        else if(dst_type == Type::HOST_FILE || dst_type == Type::MEM_FILE)
        {
            return move(src, dst);
        }
        else if(dst_type == Type::UNKNOWN)
        {
            if(exists(dst.parent_path()))
            {
                touch(dst);
                return move(src, dst);
            }
            return FSResult::NotValidPath;
        }
        return FSResult::UnknownError;
    }

    /**
     * @brief rm
     * @param src
     * @return
     *
     * Removes a file from the virtual filesystem
     * If src points to file on the host, that file will be deleted
     */
    bool rm(path_type src)
    {
        _clean(src);
        auto src_type = get_type(src);
        if(src_type == Type::MEM_FILE )
        {
            m_nodes.erase(src);
            return true;
        }
        else if(src_type == Type::HOST_FILE)
        {
            auto [it, sub] = find_parent_mount_split_it(src);
            return std::get<NodeMount>(it->second).remove(sub);
        }
        else if(src_type == Type::MEM_DIR)
        {
            if(is_empty(src) == FSResult::True)
            {
                m_nodes.erase(src);
                return true;
            }
        }
        return false;
    }

    /**
     * @brief copy
     * @param src
     * @param dst
     * @return
     *
     * Copy a file from one path to another. Both src and dst
     * must exist and must be either  HOST_FILE or a MEM_FILE.
     *
     * Dont use this: use "cp"
     */
    FSResult copy(path_type const & src, path_type const & dst)
    {
        assert(get_type(src) == Type::MEM_FILE || get_type(src) == Type::HOST_FILE);
        assert(get_type(dst) == Type::MEM_FILE || get_type(dst) == Type::HOST_FILE);

        {

            auto Fout = this->open(dst, std::ios::out);
            auto Fin  = this->open(src, std::ios::in);
            std::vector<char> _buff(1024 * 1024);

            while(!Fin.eof())
            {
                Fin.read(&_buff[0], 1024*1024 - 1);
                auto s = Fin.gcount();
                if(s==0)
                    break;
                Fout.write(&_buff[0], s);
            }
            //Fout << Fin.rdbuf();
            return FSResult::Success;
        }
    }

    /**
     * @brief move
     * @param src
     * @param dst
     * @return
     *
     * Movies a file from one path to another. Both src and dst must
     * exist and must be either a HOST_FILE or a MEM_FILE.
     *
     * Don't use this: use "mv"
     */
    FSResult move(path_type const & src, path_type const & dst)
    {
        // requirements:
        //   src must exist and must be a hostfile or a memfile
        //   dst must exist and must be a hostfile or a memfile (can be empty)

        auto src_type = get_type(src);
        auto dst_type = get_type(dst);

        assert(src_type == Type::MEM_FILE || src_type == Type::HOST_FILE);
        assert(dst_type == Type::MEM_FILE || dst_type == Type::HOST_FILE);

        if(src_type == Type::HOST_FILE && dst_type == Type::MEM_FILE)
        {
            // file exists on the host, so we'll have to
            // copy the file into memory and delete the old
            cp(src, dst);

            // todo: delete src
            return FSResult::Success;
        }
        else if(src_type == Type::HOST_FILE && dst_type == Type::HOST_FILE)
        {
            std::filesystem::rename(host_path(src), host_path(dst));
            return FSResult::Success;
        }
        else if(src_type == Type::MEM_FILE && dst_type == Type::MEM_FILE)
        {
            auto & sn = get<NodeFile>(src);
            auto & sd = get<NodeFile>(dst);
            sd.filedata = std::move(sn.filedata);
            m_nodes.erase(src);
            return FSResult::Success;
        }
        else if(src_type == Type::MEM_FILE && dst_type == Type::HOST_FILE)
        {
            cp(src, dst);
            m_nodes.erase(src);
            return FSResult::Success;
        }

        return FSResult::UnknownError;
    }

    /**
     * @brief fs
     * @param path
     * @return
     *
     * Gets a reference to a file in the system
     */
    NodeRef fs(path_type path)
    {
        return getNode(path);
    }
    NodeRef getNode(path_type path)
    {
        _clean(path);
        auto [it, sub] = find_parent_mount_split_it(path);
        if(!sub.empty())
        {
            return NodeRef {
                &it->second, sub
            };
        }

        if(it != m_nodes.end())
        {
            return NodeRef {
                &it->second,
                {}
            };
        }
        throw std::out_of_range(std::format("{} does not exist", path.string()));
    }


    template<typename T>
    T& get(path_type path)
    {
        _clean(path);
        auto it = find_node(path);
        if(it != m_nodes.end())
        {
            if(std::holds_alternative<T>(it->second))
            {
                return std::get<T>(it->second);
            }
            throw std::out_of_range(std::format("{} is not a custom file", path.string()));
        }
        throw std::out_of_range(std::format("{} does not exist", path.string()));
    }

    template<typename T>
    T const & get(path_type path) const
    {
        _clean(path);
        auto it = find_node(path);
        if(it != m_nodes.end())
        {
            if(std::holds_alternative<T>(it->second))
            {
                return std::get<T>(it->second);
            }
            throw std::out_of_range(std::format("{} is not a custom file", path.string()));
        }
        throw std::out_of_range(std::format("{} does not exist", path.string()));
    }

    /**
     * @brief host_path
     * @param path
     * @return
     *
     * Returns the path on the host file system the file exists
     */
    path_type host_path(path_type path) const
    {
        auto [it, sub] = find_parent_mount_split_it(path);
        if(sub.empty())
            return {};

        return std::get<NodeMount>(it->second).host_path / path.lexically_relative(it->first);
    }

    /**
     * @brief open
     * @param path
     * @param openmode
     * @return
     *
     * Opens a file for reading/writing
     */
    FileStream open(path_type path,  std::ios::openmode openmode)
    {
        assert(path.has_root_directory());

        auto [it, sub] = find_parent_mount_split_it(path);
        if(it == m_nodes.end())
        {
            return {};
        }
        if(!sub.empty())
        {
            // host file
            auto bff = std::make_unique<DelegatingFileStreamBuf>();
            assert(bff->open(std::get<NodeMount>(it->second).host_path / sub, openmode));
            return FileStream(std::move(bff));
            return FileStream( std::get<NodeMount>(it->second).host_path / sub, openmode);
        }
        if( std::holds_alternative<NodeFile>(it->second) )
        {
            auto bff = std::make_unique<vector_backed_streambuf>(std::get<NodeFile>(it->second).filedata);
            return FileStream(std::move(bff));
            return FileStream(std::get<NodeFile>(it->second).filedata );
        }

        return {};
    }

    /**
     * @brief get_type
     * @param path
     * @return
     *
     * Returns the type of the file
     */
    Type get_type(path_type path) const
    {
        _clean(path);
        assert(path.has_root_directory());

        auto [it, sub] = find_parent_mount_split_it(path);
        //auto it = find_node(path);
        if(!sub.empty())
        {
            auto & MNT = std::get<NodeMount>(it->second);
            if( MNT.is_dir(sub) ) return Type::HOST_DIR;
            if( MNT.is_file(sub) ) return Type::HOST_FILE;
            return Type::UNKNOWN;
        }

        if(it!=m_nodes.end())
        {
            return std::visit([](auto &&v) {
                        using value_type = std::decay_t<decltype(v)>;
                            if constexpr (std::is_same_v<value_type, NodeFile>) return Type::MEM_FILE;
                            if constexpr (std::is_same_v<value_type, NodeDir >)  return Type::MEM_DIR;
                            if constexpr (std::is_same_v<value_type, NodeMount>) return Type::MOUNT;
                            if constexpr (std::is_same_v<value_type, NodeCustom>) return Type::CUSTOM;
                        },
                       it->second);
        }

        return Type::UNKNOWN;
    }

    std::string file_to_string(path_type path)
    {
        auto in = this->open(path, std::ios::in);
        if(!in)
            return {};

        return std::string((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    }

protected:


    template<typename T>
    FSResult _mk(path_type path, bool make_parent_dirs)
    {
        _clean(path);
        auto fn = path.filename().string();
        if(fn.end() != std::find_if(fn.begin(), fn.end(), [](auto & f)
        {
            if( std::isalnum(f) || f=='.' || f=='-' || f=='_' )
                return false;
            return true;
        }))
        {
            return FSResult::InvalidFileName;
        }

        assert(path.has_root_directory());
        if(exists(path))
            return FSResult::PathExists;

        auto [it, sub] = find_parent_mount_split_it(path);
        if(!sub.empty())
        {
            return std::get<NodeMount>(it->second)._mk<T>(sub);
        }
        else if (it != m_nodes.end())
        {
            // path exists
            return FSResult::PathExists;
        }

        // path doesn't exist
        if(make_parent_dirs && !exists(path.parent_path()))
        {
            mkdir(path.parent_path());
        }

        if(!is_dir(path.parent_path()))
        {
            return FSResult::NotValidPath;
        }

        m_nodes[path] = T{};
        return FSResult::Success;
    }

    std::pair<decltype(m_nodes)::const_iterator, path_type> find_parent_mount_split_it(path_type path) const
    {
        _clean(path);
        path = path.lexically_normal();
        assert(path.has_root_directory());

        path_type root;
        auto it = m_nodes.end();
        for(auto const & p : path)
        {
            root /= p;
            it = find_node(root);
            if(it != m_nodes.end())
            {
                // found
                if(std::holds_alternative<NodeMount>(it->second))
                {
                    auto outpath =path.lexically_relative(root);
                    _clean(outpath);
                    return {it, outpath};
                }
            }
            else
            {
                return {it, {}};
            }
        }
        return {it, {}};
    }
    std::pair<decltype(m_nodes)::iterator, path_type> find_parent_mount_split_it(path_type path)
    {
        _clean(path);
        path = path.lexically_normal();
        assert(path.has_root_directory());

        path_type root;
        auto it = m_nodes.end();
        for(auto const & p : path)
        {
            root /= p;
            it = find_node(root);
            if(it != m_nodes.end())
            {
                // found
                if(std::holds_alternative<NodeMount>(it->second))
                {
                    return {it, path.lexically_relative(root)};
                }
            }
            else
            {
                return {it, {}};
            }
        }
        return {it, {}};
    }
};

}

void operator << (PseudoNix::NodeRef left, std::string_view right)
{
    (void)left;
    (void)right;
    if(std::holds_alternative<PseudoNix::NodeFile>(*left.n))
    {
        auto & F = std::get<PseudoNix::NodeFile>(*left.n);
        for(auto &r:right)
            F.filedata.push_back(r);
    }
}

#endif
