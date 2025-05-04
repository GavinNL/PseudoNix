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


class FileStream : public std::iostream {
public:
    FileStream()
    {
    }

    // String-based constructor (backed by stringstream)
    FileStream(std::stringstream * initialContent)
        : std::iostream(nullptr) {
        backing = initialContent;
        this->rdbuf(initialContent->rdbuf());
    }

    // File-based constructor (backed by fstream)
    FileStream(const std::filesystem::path& filePath, std::ios::openmode mode)
        : std::iostream(nullptr), backing(std::fstream(filePath, mode)) {
        this->rdbuf(std::get<std::fstream>(backing).rdbuf());
    }
private:
    std::variant<std::stringstream*, std::fstream> backing;
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
    std::stringstream filedata;
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

    path_type find_parent_mount(path_type p) const
    {
        auto it = m_nodes.find(p);
        if(p.empty())
            return p;
        if(std::holds_alternative<NodeMount>(it->second))
            return it->first;
        if(it == m_nodes.end())
        {
            return find_parent_mount(p.parent_path());
        }
        if(it->first == "/")
            return path_type();
        return find_parent_mount(it->first.parent_path());
    }

    std::pair<path_type, path_type> find_parent_mount_split(path_type p) const
    {
        auto mnt = find_parent_mount(p);
        if(mnt.empty())
        {
            return {{}, p};
        }
        else
        {
            auto mnt_i = m_nodes.find(mnt);
            return {mnt, p.lexically_relative(mnt_i->first)};
        }
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

        auto parent_mount_path = find_parent_mount(path);
        if(!parent_mount_path.empty())
        {
            auto mnt_i = m_nodes.find(parent_mount_path);
            return std::get<NodeMount>(mnt_i->second).is_empty( path.lexically_relative(mnt_i->first) );
        }
        else
        {
            auto it = m_nodes.find(path);
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

        auto mnt = find_parent_mount(path);
        if(!mnt.empty())
            return unexpected(FSResult::NOT_VALID_MOUNT);

        if(!is_empty(path))
            return unexpected(FSResult::NOT_EMPTY);

        auto it = m_nodes.find(path);
        it->second = NodeMount{host_path};
        return true;
    }

    expected<bool, FSResult> umount(path_type path)
    {
        auto mnt = find_parent_mount(path);
        if(mnt == path && !mnt.empty())
        {
            m_nodes.find(mnt)->second = NodeDir{};
            return true;
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
        auto [mnt_path, file_path] = find_parent_mount_split(path);
        if(!mnt_path.empty())
        {
            auto it = m_nodes.find(mnt_path);
            return NodeRef {
                &it->second, file_path
            };
        }
        auto it = m_nodes.find(path);
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
        auto mnt = find_parent_mount(path);
        if(mnt.empty())
            return {};

        auto mnt_i = m_nodes.find(mnt);
        return std::get<NodeMount>(mnt_i->second).host_path / path.lexically_relative(mnt_i->first);
    }

    FileStream open(path_type path,  std::ios::openmode openmode)
    {
        assert(path.is_absolute());
        auto typ = get_type(path);
        if(typ == Type::HOST_FILE)
        {
            return FileStream(host_path(path), openmode);
        }
        else if( typ == Type::MEM_FILE)
        {
            return FileStream(&get<NodeFile>(path).filedata);
        }
        return {};
    }

    Type get_type(path_type path) const
    {
        assert(path.is_absolute());

        auto it = m_nodes.find(path);
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
        auto [mnt, sub] = find_parent_mount_split(path);
        if(!mnt.empty())
        {
            auto & MNT = get<NodeMount>(mnt);
            if( MNT.is_dir(sub) ) return Type::HOST_DIR;
            if( MNT.is_file(sub) ) return Type::HOST_FILE;
        }
        return Type::UNKNOWN;
    }

    std::string file_to_string(path_type path) const
    {
        auto typ = get_type(path);
        if(typ == Type::HOST_FILE)
        {
            std::ifstream file( host_path(path) );
            if (!file) {
                return {};
            }
            return std::string((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());
        }
        if( typ == Type::MEM_FILE)
        {
            return get<NodeFile>(path).filedata.str();
        }
        return {};
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
            F.filedata.put(r);
    }
}

#endif
