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

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast" // Example: disable specific warning
#pragma GCC diagnostic ignored "-Wshadow" // Example: disable specific warning
#include "generator.h"
#pragma GCC diagnostic pop


#if defined __cpp_lib_expected
    #include <expected>
#else
    #include "expected.h"
#endif

namespace PseudoNix
{

#if defined __cpp_lib_expected
template<typename T, typename E>
using expected = std::expected<T,E>;

template <typename E>
using unexpected  = std::unexpected<E>;
#else
template<typename T, typename E>
using expected = tl::expected<T,E>;

template <typename E>
using unexpected  = tl::unexpected<E>;
#endif


class string_backed_streambuf : public std::streambuf {
public:
    explicit string_backed_streambuf(std::string& backing)
        : buffer(backing)
    {
        sync_with_string();
    }

protected:
    // Called when output buffer is full or a single char is put
    int overflow(int ch) override {
        if (ch != EOF) {
            std::size_t pos = static_cast<size_t>(pptr() - pbase());
            buffer.push_back(static_cast<char>(ch));
            sync_with_string(pos + 1);
        }
        return ch;
    }

    // Called when input buffer is empty
    int underflow() override {
        if (gptr() >= egptr()) return EOF;
        return static_cast<unsigned char>(*gptr());
    }

    // Called on seek operations
    std::streampos seekoff(std::streamoff off, std::ios_base::seekdir way,
                           std::ios_base::openmode which) override {
        std::streamoff new_pos = -1;

        if (which & std::ios_base::in) {
            if (way == std::ios_base::beg)
                new_pos = off;
            else if (way == std::ios_base::cur)
                new_pos = gptr() - eback() + off;
            else if (way == std::ios_base::end)
                new_pos = static_cast<std::streamoff>(buffer.size()) + off;

            if (new_pos >= 0 && static_cast<std::size_t>(new_pos) <= buffer.size()) {
                setg(&buffer[0], &buffer[0] + new_pos, &buffer[0] + buffer.size());
                return new_pos;
            }
        }

        if (which & std::ios_base::out) {
            if (way == std::ios_base::beg)
                new_pos = off;
            else if (way == std::ios_base::cur)
                new_pos = pptr() - pbase() + off;
            else if (way == std::ios_base::end)
                new_pos = static_cast<std::streamoff>(buffer.size()) + off;

            if (new_pos >= 0) {
                if (static_cast<std::size_t>(new_pos) > buffer.size())
                    buffer.resize(static_cast<size_t>(new_pos), '\0');
                sync_with_string(static_cast<std::size_t>(new_pos));
                return new_pos;
            }
        }

        return -1;
    }

    std::streampos seekpos(std::streampos sp, std::ios_base::openmode which) override {
        return seekoff(sp, std::ios_base::beg, which);
    }

    int sync() override {
        return 0;  // no-op, buffer is always in sync
    }

private:
    std::string buffer;

    void sync_with_string(std::size_t write_pos = 0) {
        // Set up get area (input)
        setg(&buffer[0], &buffer[0], &buffer[0] + buffer.size());

        // Set up put area (output)
        if (write_pos > buffer.size())
            buffer.resize(write_pos, '\0');
        setp(&buffer[0], &buffer[0] + buffer.size());
        pbump(static_cast<int>(write_pos));
    }
};

class string_backed_iostream : public std::iostream {
public:
    explicit string_backed_iostream(std::string& backing)
        : std::iostream(nullptr), buf(backing)
    {
        rdbuf(&buf);
    }

private:
    string_backed_streambuf buf;
};

class FileStream : public std::iostream {
public:
    FileStream()
    {
    }

    // String-based constructor (backed by stringstream)
    explicit FileStream(std::string &initialContent)
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
private:
    std::variant<std::unique_ptr<string_backed_iostream>, std::fstream> backing;
};

enum class FSResult
{
    SUCCESS,
    EXISTS,
    DOES_NOT_EXIST,
    NOT_EMPTY,
    NOT_VALID_MOUNT,
    HOST_DOES_NOT_EXIST,
    CANNOT_CREATE
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
    std::string filedata;
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

struct NodeMount
{
    std::filesystem::path host_path;
    bool read_only = false;

    bool exists(std::filesystem::path const & path) const
    {
        assert(path.is_relative());
        return std::filesystem::exists(host_path / path);
    }
    template<typename T>
    bool _is(std::filesystem::path const &p) const
    {
        if constexpr( std::is_same_v<T, NodeDir> )
        {
            return is_dir(p);
        }
        if constexpr( std::is_same_v<T, NodeFile> )
        {
            return is_file(p);
        }
        if constexpr( std::is_same_v<T, NodeCustom> )
        {
            return false;
        }
    }
    template<typename T>
    bool _mk(std::filesystem::path const &p)
    {
        if constexpr( std::is_same_v<T, NodeDir> )
        {
            return mkdir(p);
        }
        if constexpr( std::is_same_v<T, NodeFile> )
        {
            return touch(p);
        }
        if constexpr( std::is_same_v<T, NodeCustom> )
        {
            return false;
        }
    }
    bool is_dir(std::filesystem::path const & path) const
    {
        assert(path.is_relative());
        return std::filesystem::is_directory(host_path / path);
    }
    bool is_file(std::filesystem::path const & path) const
    {
        assert(path.is_relative());
        return std::filesystem::is_regular_file(host_path / path);
    }
    bool mkdir(std::filesystem::path const & path)
    {
        assert(path.is_relative());
        if(read_only)
            return false;
        return std::filesystem::create_directories(host_path / path);
    }
    bool touch(std::filesystem::path const & path)
    {
        assert(path.is_relative());
        if(read_only)
            return false;
        std::ofstream out(host_path/path);
        out.close();
        return true;
        //return std::filesystem::create_directories(host_path / path);
    }
    bool is_empty(std::filesystem::path const & path) const
    {
        namespace fs = std::filesystem;
        auto abs_path = host_path / path;
        for (const auto& entry : fs::directory_iterator(abs_path)) {
            (void)entry;
            return false;
        }
        return true;
    }
    std::generator<std::filesystem::path> list_dir(std::filesystem::path path) const
    {
        namespace fs = std::filesystem;
        auto abs_path = host_path / path;
        for (const auto& entry : fs::directory_iterator(abs_path)) {
            co_yield entry.path().lexically_proximate(abs_path);
        }
    }
};
using Node = std::variant<NodeDir, NodeFile, NodeCustom, NodeMount>;

void _clean(std::filesystem::path & p)
{
    if(p.filename().empty())
    {
        p = p.parent_path();
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


    /**
     * @brief exists
     * @param path
     * @return
     *
     * Checks if a path exists
     */
    bool exists(path_type path) const
    {
        _clean(path);
        path = path.lexically_normal();
        assert(path.is_absolute());

        path_type root;
        for(auto & p : path)
        {
            root /= p;
            auto it = m_nodes.find(root);
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

    expected<bool, FSResult> is_dir(path_type p)
    {
        return _is<NodeDir>(p);
    }

    expected<bool, FSResult> is_file(path_type p)
    {
        return _is<NodeFile>(p);
    }

    expected<bool, FSResult> is_custom_file(path_type p)
    {
        return _is<NodeCustom>(p);
    }

    std::generator<path_type> list_dir(path_type path) const
    {
        _clean(path);
        auto [it, sub] = find_parent_mount_split_it(path);

        //auto it = m_nodes.find(path);
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

    expected<bool, FSResult> mkdir(path_type path)
    {
        return _mk<NodeDir>(path, true);
    }

    expected<bool, FSResult> mkcustom(path_type path)
    {
        return _mk<NodeCustom>(path, false);
    }

    expected<bool, FSResult> touch(path_type path)
    {
        return _mk<NodeFile>(path, false);
    }

    expected<bool, FSResult> is_empty(path_type path) const
    {
        _clean(path);
        auto [it, sub] = find_parent_mount_split_it(path);

        if(!sub.empty())
        {
            return std::get<NodeMount>(it->second).is_empty( sub );
        }
        else
        {
            if(it != m_nodes.end())
            {
                auto next = std::next(it);
                if(next == m_nodes.end())
                    return true;

                if(!std::holds_alternative<NodeDir>(it->second))
                {
                    return false;
                }
                if(it->first.lexically_relative(next->first) == "..")
                {
                    return false;
                }
                return true;
            }
            else
            {
                return unexpected(FSResult::DOES_NOT_EXIST);
            }
        }
    }

    expected<bool, FSResult> mount( path_type host_path, path_type path)
    {
        if( !std::filesystem::is_directory(host_path))
        {
            return unexpected(FSResult::HOST_DOES_NOT_EXIST);
        }

        auto [it, sub] = find_parent_mount_split_it(path);

        // that path already exists inside
        // a mount point
        if(!sub.empty())
            return unexpected(FSResult::NOT_VALID_MOUNT);

        if(!is_empty(path))
            return unexpected(FSResult::NOT_EMPTY);

        if( std::holds_alternative<NodeDir>(it->second))
        {
            it->second = NodeMount{host_path};
            return true;
        }
        return unexpected(FSResult::NOT_VALID_MOUNT);
    }

    expected<bool, FSResult> umount(path_type path)
    {
        auto [it, sub] = find_parent_mount_split_it(path);
        //auto mnt = find_parent_mount(path);
        if(it != m_nodes.end() && sub.empty())
        {
            if(std::holds_alternative<NodeMount>(it->second))
            {
                it->second = NodeDir{};
                return true;
            }
        }

        return unexpected(FSResult::NOT_VALID_MOUNT);
    }

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
        throw std::out_of_range(std::format("{} does not exist", path.c_str()).c_str());
    }


    template<typename T>
    T& get(path_type path)
    {
        _clean(path);
        auto it = m_nodes.find(path);
        if(it != m_nodes.end())
        {
            if(std::holds_alternative<T>(it->second))
            {
                return std::get<T>(it->second);
            }
            throw std::out_of_range(std::format("{} is not a custom file", path.c_str()).c_str());
        }
        throw std::out_of_range(std::format("{} does not exist", path.c_str()).c_str());
    }

    template<typename T>
    T const & get(path_type path) const
    {
        _clean(path);
        auto it = m_nodes.find(path);
        if(it != m_nodes.end())
        {
            if(std::holds_alternative<T>(it->second))
            {
                return std::get<T>(it->second);
            }
            throw std::out_of_range(std::format("{} is not a custom file", path.c_str()).c_str());
        }
        throw std::out_of_range(std::format("{} does not exist", path.c_str()).c_str());
    }

    path_type host_path(path_type path) const
    {
        auto [it, sub] = find_parent_mount_split_it(path);
        if(sub.empty())
            return {};

        return std::get<NodeMount>(it->second).host_path / path.lexically_relative(it->first);
    }

    FileStream open(path_type path,  std::ios::openmode openmode)
    {
        assert(path.is_absolute());

        auto [it, sub] = find_parent_mount_split_it(path);
        if(it == m_nodes.end())
        {
            return {};
        }
        if(!sub.empty())
        {
            // host file
            return FileStream( std::get<NodeMount>(it->second).host_path / sub, openmode);
        }
        if( std::holds_alternative<NodeFile>(it->second) )
        {
            return FileStream(std::get<NodeFile>(it->second).filedata);
        }

        return {};
    }

    Type get_type(path_type path) const
    {
        assert(path.is_absolute());

        auto [it, sub] = find_parent_mount_split_it(path);
        //auto it = m_nodes.find(path);
        if(!sub.empty())
        {
            auto & MNT = std::get<NodeMount>(it->second);
            if( MNT.is_dir(sub) ) return Type::HOST_DIR;
            if( MNT.is_file(sub) ) return Type::HOST_FILE;
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
    expected<bool, FSResult> _is(path_type p)
    {
        _clean(p);
        auto [it, sub] = find_parent_mount_split_it(p);
        if( it != m_nodes.end())
        {
            if(std::holds_alternative<NodeMount>(it->second))
                return std::get<NodeMount>(it->second)._is<T>(sub);
            return std::holds_alternative<T>(it->second);
        }
        return unexpected(FSResult::DOES_NOT_EXIST);
    }

    template<typename T>
    expected<bool, FSResult> _mk(path_type path, bool make_parent_dirs)
    {
        _clean(path);
        assert(path.is_absolute());
        if(exists(path))
            return false;

        auto [it, sub] = find_parent_mount_split_it(path);
        if(!sub.empty())
        {
            return std::get<NodeMount>(it->second)._mk<T>(sub);
        }
        else if (it != m_nodes.end())
        {
            // path exists
            return unexpected(FSResult::EXISTS);
        }

        // path doesn't exist
        if(make_parent_dirs && !exists(path.parent_path()))
        {
            mkdir(path.parent_path());
        }

        if(!is_dir(path.parent_path()))
        {
            return false;
        }

        m_nodes[path] = T{};
        return true;
    }

    std::pair<decltype(m_nodes)::const_iterator, path_type> find_parent_mount_split_it(path_type path) const
    {
        _clean(path);
        path = path.lexically_normal();
        assert(path.is_absolute());

        path_type root;
        auto it = m_nodes.end();
        for(auto & p : path)
        {
            root /= p;
            it = m_nodes.find(root);
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
    std::pair<decltype(m_nodes)::iterator, path_type> find_parent_mount_split_it(path_type path)
    {
        _clean(path);
        path = path.lexically_normal();
        assert(path.is_absolute());

        path_type root;
        auto it = m_nodes.end();
        for(auto & p : path)
        {
            root /= p;
            it = m_nodes.find(root);
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
