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


enum class FSResult
{
    SUCCESS,
    EXISTS,
    DOES_NOT_EXIST,
    NOT_EMPTY,
    NOT_VALID_MOUNT,
    CANNOT_CREATE
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

    bool exists(std::filesystem::path const & path) const
    {
        assert(path.is_relative());
        return std::filesystem::exists(host_path / path);
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
        return std::filesystem::create_directories(host_path / path);
    }
    bool touch(std::filesystem::path const & path)
    {
        assert(path.is_relative());
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
struct FileSystem
{
    using node_type = Node;
    using path_type = std::filesystem::path;

    std::map<path_type, node_type> m_nodes;

    FileSystem()
    {
        m_nodes["/"] = NodeDir{ };
    }

    bool exists(path_type p) const
    {
        _clean(p);
        auto it = m_nodes.find(p);
        if(it != m_nodes.end())
            return true;

        auto mnt = find_parent_mount(p);
        if(mnt.empty())
            return false;

        auto mnt_i = m_nodes.find(mnt);
        return std::get<NodeMount>(mnt_i->second).exists( p.lexically_relative(mnt_i->first) );
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

    expected<bool, FSResult> is_dir(path_type p)
    {
        _clean(p);
        auto it = m_nodes.find(p);
        if(it != m_nodes.end())
        {
            return std::holds_alternative<NodeDir>(it->second);
        }

        auto mnt = find_parent_mount(p);
        if(mnt.empty())
            return unexpected(FSResult::DOES_NOT_EXIST);

        auto mnt_i = m_nodes.find(mnt);
        return std::get<NodeMount>(mnt_i->second).is_dir( p.lexically_relative(mnt_i->first) );
    }

    expected<bool, FSResult> is_file(path_type p)
    {
        _clean(p);
        auto it = m_nodes.find(p);
        if(it != m_nodes.end())
        {
            return std::holds_alternative<NodeFile>(it->second);
        }

        auto mnt = find_parent_mount(p);
        if(mnt.empty())
            return false;

        auto mnt_i = m_nodes.find(mnt);
        return std::get<NodeMount>(mnt_i->second).is_file( p.lexically_relative(mnt_i->first) );
    }

    expected<bool, FSResult> is_custom_file(path_type p)
    {
        _clean(p);
        auto it = m_nodes.find(p);
        if(it != m_nodes.end())
        {
            return std::holds_alternative<NodeCustom>(it->second);
        }
        return false;
    }

    std::generator<path_type> list_dir(path_type path)
    {
        _clean(path);
        auto it = m_nodes.find(path);
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
        }

        auto mnt = find_parent_mount(path);
        if(mnt.empty())
            co_return;

        auto mnt_i = m_nodes.find(mnt);
        auto ll = std::get<NodeMount>(mnt_i->second).list_dir( path.lexically_relative(mnt_i->first) );
        for(auto D : ll)
        {
            co_yield mnt_i->first / D;
        }
    }

    template<typename node_type>
    expected<bool, FSResult> mknode(path_type path)
    {
        _clean(path);
        if(exists(path))
            return false;

        auto parent_mount_path = find_parent_mount(path);
        if(!parent_mount_path.empty())
        {
            auto mnt_i = m_nodes.find(parent_mount_path);
            return std::get<NodeMount>(mnt_i->second).mkdir( path.lexically_relative(mnt_i->first) );
        }
        else
        {
            if(!exists(path.parent_path()))
            {
                mkdir(path.parent_path());
            }
            if(!is_dir(path.parent_path()))
            {
                return false;
            }
            m_nodes[path] = node_type{};
            return true;
        }
        return unexpected(FSResult::EXISTS);
    }

    expected<bool, FSResult> mkdir(path_type path)
    {
        _clean(path);
        if(exists(path))
            return false;

        auto parent_mount_path = find_parent_mount(path);
        if(!parent_mount_path.empty())
        {
            auto mnt_i = m_nodes.find(parent_mount_path);
            return std::get<NodeMount>(mnt_i->second).mkdir( path.lexically_relative(mnt_i->first) );
        }
        else
        {
            if(!exists(path.parent_path()))
            {
                mkdir(path.parent_path());
            }
            if(!is_dir(path.parent_path()))
            {
                return false;
            }
            m_nodes[path] = NodeDir{};
            return true;
        }
        return unexpected(FSResult::EXISTS);
    }

    expected<bool, FSResult> mkcustom(path_type path)
    {
        _clean(path);
        if(exists(path))
            return false;

        auto parent_mount_path = find_parent_mount(path);
        if(!parent_mount_path.empty())
        {
            return unexpected(FSResult::CANNOT_CREATE);
        }
        else
        {
            if(!exists(path.parent_path()))
            {
                mkdir(path.parent_path());
            }
            if(!is_dir(path.parent_path()))
            {
                return false;
            }
            m_nodes[path] = NodeCustom{};
            return true;
        }
        return unexpected(FSResult::EXISTS);
    }

    expected<bool, FSResult> touch(path_type path)
    {
        _clean(path);
        if(exists(path))
            return false;

        auto parent_mount_path = find_parent_mount(path);
        if(!parent_mount_path.empty())
        {
            auto mnt_i = m_nodes.find(parent_mount_path);
            return std::get<NodeMount>(mnt_i->second).touch( path.lexically_relative(mnt_i->first) );
        }
        else
        {
            if(!exists(path.parent_path()))
            {
                mkdir(path.parent_path());
            }
            if(!is_dir(path.parent_path()))
            {
                return false;
            }
            m_nodes[path] = NodeFile{};
            return true;
        }
        return unexpected(FSResult::EXISTS);
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

    expected<bool, FSResult> mount(path_type path, path_type host_path)
    {
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

    NodeCustom& get_custom(path_type path)
    {
        _clean(path);
        auto it = m_nodes.find(path);
        if(it != m_nodes.end())
        {
            if(std::holds_alternative<NodeCustom>(it->second))
            {
                return std::get<NodeCustom>(it->second);
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

    std::string to_string(path_type path)
    {
        std::ifstream file( host_path(path) );
        if (!file) {
            return {};
        }

        return std::string((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
    }
};

}
#endif
