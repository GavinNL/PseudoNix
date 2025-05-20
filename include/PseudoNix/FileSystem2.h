#ifndef PSEUDONIX_FILESYSTEM2_H
#define PSEUDONIX_FILESYSTEM2_H

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
#include "FileSystem.h"
#include <algorithm>

namespace PseudoNix
{

inline std::pair<std::filesystem::path, std::filesystem::path> split_first(const std::filesystem::path& input) {
    auto it = input.begin();
    if (it == input.end()) {
        return {{}, {}};  // Empty input
    }

    std::filesystem::path first = *it++;
    std::filesystem::path rest;
    while (it != input.end()) {
        rest /= *it++;
    }

    return {first, rest};
}

enum FSResult2
{
    False,
    True,
    UnknownError = 255,
};


struct FSNode
{
    using path_type = std::filesystem::path;
    using result_type = FSResult2;
    std::string  name;

    FSNode()
    {
    }
    FSNode(std::string _name)
    {
        name = _name;
    }
    virtual ~FSNode()
    {
    }

    virtual result_type exists(path_type relPath) const = 0;
    virtual result_type mkdir(path_type relPath) = 0;
    virtual result_type mkfile(path_type relPath) = 0;
    virtual Generator<path_type> list_dir(path_type relPath) = 0;

    /**
     * @brief open
     * @param relPath
     * @param mode
     * @return
     *
     * return an opened streambuf to the file indicated by relpath
     */
    virtual std::unique_ptr<std::streambuf> open(path_type relPath, std::ios::openmode mode) = 0;
};

struct FSNodeFile : public FSNode
{
    std::vector<char> data;

    FSNodeFile(std::string const & _name) : FSNode(_name)
    {
    }

    virtual result_type exists(path_type relPath) const override
    {
        if(relPath.empty())
            return result_type::True;
        return result_type::False;
    }

    virtual result_type mkdir(path_type relPath) override
    {
        (void)relPath;
        return result_type::False;
    }

    virtual result_type mkfile(path_type relPath) override
    {
        (void)relPath;
        return result_type::False;
    }

    virtual Generator<path_type> list_dir(path_type relPath) override
    {
        (void)relPath;
        co_return;
    }


    virtual std::unique_ptr<std::streambuf> open(path_type relPath, std::ios::openmode mode) override
    {
        (void)relPath;
        (void)mode;
        // RelPath must be empty because this is a file node
        assert(relPath.empty());
        auto bff = std::make_unique<vector_backed_streambuf>(data);
        return bff;
    }
};


struct FSNodeDir : public FSNode
{
    std::map<std::string, std::shared_ptr<FSNode> > nodes;

    // a mount node
    std::shared_ptr<FSNode> mount;

    FSNodeDir(std::string const & _name) : FSNode(_name)
    {
    }
    virtual bool contains(std::string node_name) const
    {
        return nodes.contains(node_name);
    }

    virtual std::shared_ptr<FSNode> find(path_type relPath)
    {
        assert(!relPath.has_root_directory());

        auto [first, remaining] = split_first(relPath);
        auto first_str = first.generic_string();

        if(remaining.empty())
        {
            auto it = nodes.find(first_str);
            if(it != nodes.end())
                return it->second;
        }
        else
        {
            // its a folder
            auto it = nodes.find(first_str);
            if(it != nodes.end())
            {
                auto dir = std::dynamic_pointer_cast<FSNodeDir>(it->second);
                if(dir)
                    return dir->find(remaining);
            }
        }
        return nullptr;
    }

    /**
     * @brief find_valid
     * @param relPath
     * @return
     *
     * Given a path /path/to/some/file
     *
     * If /path/to exists, but some/file does not
     * it will return a pointer to /path/to
     *
     */
    std::pair<std::shared_ptr<FSNode>, path_type> find_valid(path_type relPath)
    {
        assert(!relPath.has_root_directory());

        auto [first, remaining] = split_first(relPath);
        auto first_str = first.generic_string();

        if(remaining.empty())
        {
            auto it = nodes.find(first_str);
            if(it != nodes.end())
                return {it->second, {}};
        }
        else
        {
            // its a folder
            auto it = nodes.find(first_str);
            if(it != nodes.end())
            {
                auto dir = std::dynamic_pointer_cast<FSNodeDir>(it->second);
                if(dir)
                {
                    return dir->find_valid(remaining);
                }
                return {it->second, remaining};
            }
        }
        return {};
    }

    result_type exists(path_type relPath) const override
    {
        if(mount)
            return mount->exists(relPath);
        assert(!relPath.has_root_directory());
        // an empty path always means
        // this current node, which has to exist
        if(relPath.empty())
            return result_type::True;

        auto [first, remaining] = split_first(relPath);
        auto first_str = first.generic_string();

        if(remaining.empty())
        {
            // its a file or a folder
            bool ex = nodes.contains(first_str);
            return ex ? result_type::True : result_type::False;
        }
        else
        {
            // its a folder
            auto it = nodes.find(first_str);
            if(it == nodes.end())
                return result_type::False;
            return it->second->exists(remaining);
        }
    }


    result_type mkdir(path_type relPath) override
    {
        assert(!relPath.has_root_directory());
        if(mount)
            return mount->mkdir(relPath);
        // an empty path always means
        // this current node, which has to exist
        if(relPath.empty())
            return result_type::True;

        auto [first, remaining] = split_first(relPath);
        auto first_str = first.generic_string();

        if(remaining.empty())
        {
            // its a file or a folder
            if(nodes.contains(first_str))
            {
                return result_type::False;
            }
            auto ptr = std::make_shared<FSNodeDir>(first_str);
            nodes[first_str] = ptr;
            return result_type::True;
        }
        else
        {
            // its a folder
            auto it = nodes.find(first_str);
            if(it != nodes.end())
            {
                auto dir = std::dynamic_pointer_cast<FSNodeDir>(it->second);
                return it->second->mkdir(remaining);
            }
        }
        return result_type::False;
    }
    virtual result_type mkfile(path_type relPath) override
    {
        assert(!relPath.has_root_directory());
        if(mount)
            return mount->mkfile(relPath);
        // an empty path always means
        // this current node, which has to exist
        if(relPath.empty())
            return result_type::True;

        auto [first, remaining] = split_first(relPath);
        auto first_str = first.generic_string();

        if(remaining.empty())
        {
            // its a file or a folder
            if(nodes.contains(first_str))
            {
                return result_type::False;
            }
            auto ptr = std::make_shared<FSNodeFile>(first_str);
            nodes[first_str] = ptr;
            return result_type::True;
        }
        else
        {
            // its a folder
            auto it = nodes.find(first_str);
            if(it != nodes.end())
            {
                auto dir = std::dynamic_pointer_cast<FSNodeDir>(it->second);
                return it->second->mkfile(remaining);
            }
        }
        return result_type::False;
    }
    virtual Generator<path_type> list_dir(path_type relPath) override
    {
        if(mount)
        {
            for(auto n : mount->list_dir(relPath))
            {
                co_yield n;
            }
            co_return;
        }
        if(relPath.empty())
        {
            for(auto & n : nodes)
            {
                co_yield n.first;
            }
        }
        else
        {
            auto d = find(relPath);
            if(!d)
                co_return;
            auto dir = std::dynamic_pointer_cast<FSNodeDir>(d);
            if(dir)
            {
                for(auto & n : dir->nodes)
                {
                    co_yield n.first;
                }
            }
        }
    }

    virtual std::unique_ptr<std::streambuf> open(path_type relPath, std::ios::openmode mode) override
    {
        assert(!relPath.has_root_directory());
        if(mount)
            return mount->open(relPath, mode);
        // an empty path always means
        // this current node, which has to exist
        if(relPath.empty())
            return nullptr;

        auto [first, remaining] = split_first(relPath);
        auto first_str = first.generic_string();

        auto it = nodes.find(first);
        if(it != nodes.end())
        {
            return it->second->open(remaining, mode);
        }
        return nullptr;
    }

    virtual result_type rm(path_type relPath)
    {
        assert(!relPath.has_root_directory());
        auto [first, remaining] = split_first(relPath);
        auto first_str = first.generic_string();

        if(remaining.empty())
        {
            // first_str needs to be deleted
            auto it = nodes.find(first_str);
            if(it == nodes.end())
            {
                // file/folder doesn't exist
                return result_type::False;
            }
            auto f_p = std::dynamic_pointer_cast<FSNodeFile>(it->second);
            if(f_p)
            {
                // can erase
                nodes.erase(it);
                return result_type::True;
            }
            auto d_p = std::dynamic_pointer_cast<FSNodeDir>(it->second);
            if(d_p)
            {
                // not empty. cannot delete
                if(!d_p->nodes.empty()) return result_type::False;
                nodes.erase(it);
                return result_type::True;
            }
            return result_type::UnknownError;
        }
        else
        {
            auto it = nodes.find(first_str);
            //  dones't exist
            if(it == nodes.end())
                return result_type::False;

            auto d_p = std::dynamic_pointer_cast<FSNodeDir>(it->second);
            if(d_p)
            {
                return d_p->rm(remaining);
            }
            return result_type::UnknownError;
        }
    }
};

/**
 * @brief The FSNodeHostMount class
 *
 * A node for mounting host directories
 */
struct FSNodeHostMount : public FSNode
{
    std::filesystem::path m_path_on_host;
    FSNodeHostMount(std::filesystem::path path_on_host) : FSNode(), m_path_on_host(path_on_host)
    {
    }

    virtual result_type exists(path_type relPath) const override
    {
        auto b = std::filesystem::exists( m_path_on_host / relPath );
        return b ? result_type::True : result_type::False;
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
    virtual Generator<path_type> list_dir(path_type relPath) override
    {
        namespace fs = std::filesystem;
        auto abs_path = m_path_on_host / relPath;
        for (const auto& entry : fs::directory_iterator(abs_path)) {
            co_yield entry.path().lexically_proximate(abs_path);
        }
    }

    virtual std::unique_ptr<std::streambuf> open(path_type relPath, std::ios::openmode mode) override
    {
        (void)mode;
        auto p = std::make_unique<DelegatingFileStreamBuf>();
        p->open(m_path_on_host / relPath, mode);
        return p;
    }
};

struct FileSystem2
{
    using path_type   = FSNode::path_type;
    using result_type = FSNode::result_type;

    result_type exists(path_type abs_path)
    {
        _clean(abs_path);
        assert(abs_path.has_root_directory());

        auto [mnt, rem ] = find_last_valid_virtual_node(abs_path);
        if(mnt && rem.empty())
        {
            return result_type::True;
        }

        auto d = std::dynamic_pointer_cast<FSNodeDir>(mnt);
        if(d)
        {
            if(d->mount)
            {
                return d->mount->exists(rem);
            }
            return result_type::False;
        }
        else
        {
            // its a file for some reason?
        }
        return result_type::False;
    }
    result_type mkdir(path_type abs_path)
    {
        _clean(abs_path);
        assert(abs_path.has_root_directory());

        auto rel_path_to_root = abs_path.relative_path();

        auto [mnt, rem ] = find_last_valid_virtual_node(abs_path);

        if(!mnt)
            return result_type::UnknownError;

        if(rem.empty())
        {
            // location already exists
            return result_type::False;
        }


        auto d = std::dynamic_pointer_cast<FSNodeDir>(mnt);
        if(d)
        {
            if(d->mount)
            {
                return d->mount->mkdir(rem);
            }
            else
            {
                if( !rem.parent_path().empty() )
                {
                    // parent directory doesn't exist
                    return result_type::False;
                }
                d->nodes[rem] = std::make_shared<FSNodeDir>(rem);
                return result_type::True;
            }
        }
        return result_type::False;
    }
    result_type mkfile(path_type abs_path)
    {
        _clean(abs_path);
        assert(abs_path.has_root_directory());

        auto rel_path_to_root = abs_path.relative_path();

        auto [mnt, rem ] = find_last_valid_virtual_node(abs_path);

        if(!mnt)
            return result_type::UnknownError;

        if(rem.empty())
        {
            // location already exists
            return result_type::False;
        }

        auto d = std::dynamic_pointer_cast<FSNodeDir>(mnt);
        if(d)
        {
            if(d->mount)
            {
                return d->mount->mkfile(rem);
            }
            else
            {
                if( !rem.parent_path().empty() )
                {
                    // parent directory doesn't exist
                    return result_type::False;
                }

                d->nodes[rem] = std::make_shared<FSNodeFile>(rem);
                return result_type::True;
            }
        }
        return result_type::False;
    }
    result_type rm(path_type abs_path)
    {
        _clean(abs_path);
        assert(abs_path.has_root_directory());

        auto rel_path_to_root = abs_path.relative_path();

        return m_rootNode->rm(rel_path_to_root);
    }

    result_type unmount(path_type abs_path)
    {
        _clean(abs_path);
        assert(abs_path.has_root_directory());

        auto rel_path_to_root = abs_path.relative_path();
        auto r = m_rootNode;

        auto p = m_rootNode->find(rel_path_to_root);
        auto first_dir = std::dynamic_pointer_cast<FSNodeDir>(p);
        if(first_dir)
        {
            if(first_dir->mount)
            {
                first_dir->mount = {};
                return result_type::True;
            }
        }
        return result_type::False;
    }

    template<typename _Tp, typename... _Args>
    result_type mount(path_type abs_path_in_vfs, _Args&&... __args)
    {
        auto node_p = std::make_shared<_Tp>(std::forward<_Args>(__args)...);

        auto rel_path_to_root = abs_path_in_vfs.relative_path();

        auto node_mount = m_rootNode->find(rel_path_to_root);
        if(!node_mount)
        {
            // mount point does not exist
            return result_type::False;
        }
        auto dir = std::dynamic_pointer_cast<FSNodeDir>(node_mount);
        if(!dir)
        {
            // not a directory
            return result_type::False;
        }

        if( dir->nodes.size() > 0)
        {
            // not empty
            return result_type::False;
        }
        if(dir->mount)
            return result_type::False;

        dir->mount = node_p;
        return result_type::True;

        auto parent_dir = rel_path_to_root.parent_path();
        auto parent_node = parent_dir.empty() ? m_rootNode : m_rootNode->find(parent_dir);
        assert(parent_node);
        auto parent_node_dir = std::dynamic_pointer_cast<FSNodeDir>(parent_node);

        assert(parent_node_dir->nodes.at(abs_path_in_vfs.filename()) == node_mount);
        parent_node_dir->nodes[abs_path_in_vfs.filename()] = node_p;
        return result_type::True;
    }


    /**
     * @brief find_last_valid_virtual_dir
     * @param abs_path
     * @return
     *
     * Given a path: /path/to/some/file
     *
     * return the node to the first path that contains a mount
     */
    std::pair<std::shared_ptr<FSNode>, path_type> find_last_valid_virtual_node(path_type abs_path)
    {
        _clean(abs_path);
        assert(abs_path.has_root_directory());

        auto rel_path_to_root = abs_path.relative_path();
        auto r = m_rootNode;

        //path_type rem = rel_path_to_root;

        while(true)
        {
            auto [first, rem] = split_first(rel_path_to_root);

            auto it = r->nodes.find(first);
            if(it == r->nodes.end())
            {
                if(rem.empty())
                    return {r,first};
                return {r, first/rem};
            }
            auto first_dir = std::dynamic_pointer_cast<FSNodeDir>(it->second);
            if(first_dir)
            {
                if(first_dir->mount)
                {
                    return {first_dir, rem};
                }
                else
                {
                    r = first_dir;
                    rel_path_to_root = rem;
                }
            }
            else
            {
                // its a file
                return {r, rem};
            }
        }
    }

    std::pair<std::shared_ptr<FSNode>, path_type> find_valid(path_type abs_path)
    {
        _clean(abs_path);
        assert(abs_path.has_root_directory());
        if(abs_path == "/")
            return {m_rootNode, {}};
        auto rel_path_to_root = abs_path.relative_path();
        return m_rootNode->find_valid(rel_path_to_root);
    }

    generator<path_type> list_dir(path_type abs_path)
    {
        _clean(abs_path);
        assert(abs_path.has_root_directory());

        auto p = find_valid(abs_path);
        return p.first->list_dir({});
    }

    FileStream open(path_type abs_path,  std::ios::openmode openmode)
    {
        _clean(abs_path);
        assert(abs_path.has_root_directory());
        auto rel_path_to_root = abs_path.relative_path();

        auto [mnt, rem ] = find_last_valid_virtual_node(abs_path);
        auto bff = mnt->open(rem, openmode);
        return FileStream(std::move(bff));
    }

    std::shared_ptr<FSNodeDir> m_rootNode = std::make_shared<FSNodeDir>("/");
};

}
#endif
