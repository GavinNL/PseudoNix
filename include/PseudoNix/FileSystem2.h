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
};

struct FSNodeFile : public FSNode
{
    std::vector<char> data;

    FSNodeFile(std::string const & _name) : FSNode(_name)
    {
    }
};

enum class NodeType2 {
    Unknown,
    MemFile,
    MemDir,
    MountFile,
    MountDir,
    NoExist
};

struct FSMountBase
{
    using path_type = std::filesystem::path;
    using result_type = FSResult2;
    virtual ~FSMountBase()
    {

    }
    virtual result_type exists(path_type relPath) const = 0;
    virtual result_type mkdir(path_type relPath) = 0;
    virtual result_type mkfile(path_type relPath) = 0;
    virtual result_type rm(path_type relPath) = 0;
    virtual std::unique_ptr<std::streambuf> open(path_type relPath, std::ios::openmode mode) = 0;
    virtual NodeType2 getType(path_type relPath) const = 0;
};

struct FSNodeDir : public FSNode
{
    std::map<std::string, std::shared_ptr<FSNode> > nodes;

    // a mount node
    std::shared_ptr<FSMountBase> mount;

    FSNodeDir(std::string const & _name) : FSNode(_name)
    {
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

    virtual NodeType2 getType(path_type relPath) const override
    {
        if( std::filesystem::is_directory(m_path_on_host / relPath) )
        {
            return NodeType2::MountDir;
        }
        if( std::filesystem::is_regular_file(m_path_on_host / relPath) )
        {
            return NodeType2::MountFile;
        }
        return NodeType2::NoExist;
    }

    virtual result_type exists(path_type relPath) const override
    {
        auto b = std::filesystem::exists( m_path_on_host / relPath );
        return b ? result_type::True : result_type::False;
    }

    virtual result_type rm(path_type relPath) override
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

        auto [mnt, rem] = find_last_valid_virtual_node(abs_path.parent_path());

        auto d = std::dynamic_pointer_cast<FSNodeDir>(mnt);
        if(d)
        {
            if(d->mount)
            {
                return d->mount->rm(rem / abs_path.filename());
            }
            else
            {
                auto it = d->nodes.find(abs_path.filename());
                if(it != d->nodes.end())
                {
                    if(auto cp = std::dynamic_pointer_cast<FSNodeDir>(it->second) )
                    {
                        if(!cp->nodes.empty())
                            return result_type::False;
                        if(cp->mount)
                            return result_type::False;
                        d->nodes.erase(it);
                        return result_type::True;
                    }
                    else
                    {
                        d->nodes.erase(it);
                        return result_type::True;
                    }
                    return result_type::True;
                }
                // doens't exist
                return result_type::False;
            }
        }
        return result_type::False;
    }

    result_type unmount(path_type abs_path_in_vfs)
    {
        auto [mnt, rem ] = find_last_valid_virtual_node(abs_path_in_vfs);

        if( mnt && rem.empty())
        {
            auto dir = std::dynamic_pointer_cast<FSNodeDir>(mnt);
            if(!dir)
            {
                // not a directory
                return result_type::False;
            }

            // nothing mounted
            if(!dir->mount)
                return result_type::False;

            dir->mount = {};
            return result_type::True;
        }
        return result_type::False;
    }

    template<typename _Tp, typename... _Args>
    result_type mount(path_type abs_path_in_vfs, _Args&&... __args)
    {

        auto [mnt, rem ] = find_last_valid_virtual_node(abs_path_in_vfs);

        if( mnt && rem.empty())
        {
            auto dir = std::dynamic_pointer_cast<FSNodeDir>(mnt);
            if(!dir)
            {
                // not a directory
                return result_type::False;
            }

            // not empty
            if(dir->nodes.size() > 0)
                return result_type::False;

            // already mounted
            if(dir->mount)
                return result_type::False;

            dir->mount = std::make_shared<_Tp>(std::forward<_Args>(__args)...);
            return result_type::True;
        }
        return result_type::False;
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
    std::pair<std::shared_ptr<const FSNode>, path_type> find_last_valid_virtual_node(path_type abs_path) const {
        return const_cast<FileSystem2*>(this)->find_last_valid_virtual_node(abs_path);
    }
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
                return {it->second, rem};
            }
        }
    }

    result_type move(path_type srcAbsPath, path_type dstAbsPath)
    {
        if(!exists(srcAbsPath))
        {
            // src file doesn't exist
            return result_type::False;
        }
        if(!exists(dstAbsPath.parent_path()))
        {
            // parent folder of dst doesnt exist
            return result_type::False;
        }

        auto tSrc = getType(srcAbsPath);
        auto tDst = getType(dstAbsPath);

        if( tDst == NodeType2::MemDir || tDst == NodeType2::MountDir)
            dstAbsPath = dstAbsPath / srcAbsPath.filename();

        auto tDstFolder = getType(dstAbsPath.parent_path());

        if(tSrc == NodeType2::MemFile && tDstFolder == NodeType2::MemDir )
        {
            auto [srcMnt, srcRem ] = find_last_valid_virtual_node(srcAbsPath);
            auto [dstMnt, dstRem ] = find_last_valid_virtual_node(dstAbsPath.parent_path());

            auto srcFile_p = std::dynamic_pointer_cast<FSNodeFile>(srcMnt);
            auto dstDir_p  = std::dynamic_pointer_cast<FSNodeDir>(dstMnt);

            dstDir_p->nodes[dstAbsPath.filename()] = srcFile_p;

            rm(srcAbsPath);

            return result_type::True;
        }
        else
        {
            // do the long way around, copy+delete
            auto ret = copy(srcAbsPath, dstAbsPath);
            if(ret != result_type::True)
                return ret;
            ret = rm(srcAbsPath);
            if(ret != result_type::True)
                return ret;
            return result_type::True;
        }
        return result_type::False;
    }

    result_type copy(path_type  srcAbsPath, path_type  dstAbsPath)
    {
        if(!exists(srcAbsPath))
        {
            // src file doesn't exist
            return result_type::False;
        }

        auto dType = getType(dstAbsPath);
        if(dType == NodeType2::MemDir || dType == NodeType2::MountDir)
        {
            dstAbsPath = dstAbsPath / srcAbsPath.filename();
        }

        if(!exists(dstAbsPath))
        {
            auto v = mkfile(dstAbsPath);
            if(v != result_type::True)
                return result_type::False; // cannot create dst file
        }
        auto Fout = this->open(dstAbsPath, std::ios::out | std::ios::binary);
        auto Fin  = this->open(srcAbsPath, std::ios::in | std::ios::binary);

        if(!Fout.good() )
            return result_type::False;
        if(!Fin.good() )
            return result_type::False;

        std::vector<char> _buff(1024 * 1024);

        while(!Fin.eof())
        {
            Fin.read(&_buff[0], 1024*1024 - 1);
            auto s = Fin.gcount();
            if(s==0)
                break;
            Fout.write(&_buff[0], s);
        }

        return result_type::True;
    }


    FileStream open(path_type abs_path,  std::ios::openmode openmode)
    {
        _clean(abs_path);
        assert(abs_path.has_root_directory());
        auto rel_path_to_root = abs_path.relative_path();

        auto [mnt, rem ] = find_last_valid_virtual_node(abs_path);

        if(rem.empty())
        {
            if(auto f = std::dynamic_pointer_cast<FSNodeFile>(mnt))
            {
                auto bff = std::make_unique<vector_backed_streambuf>(f->data);
                return FileStream(std::move(bff));
            }
            // an empty folder cannot open
            return FileStream();
        }
        else
        {
            if(auto d = std::dynamic_pointer_cast<FSNodeDir>(mnt))
            {
                if(d->mount)
                {
                    auto bff = d->mount->open(rem, openmode);
                    return FileStream(std::move(bff));
                }
            }
        }
        return {};
    }

    NodeType2 getType(path_type absPath) const
    {
        auto [mnt, rem] = find_last_valid_virtual_node(absPath);

        if(rem.empty())
        {
            if(auto d = std::dynamic_pointer_cast<const FSNodeDir>(mnt))
            {
                if(d->mount)
                    return d->mount->getType(rem);
                return NodeType2::MemDir;
            }
            return NodeType2::MemFile;
        }
        if(auto d = std::dynamic_pointer_cast<const FSNodeDir>(mnt))
        {
            if(d->mount)
                return d->mount->getType(rem);
        }
        return NodeType2::NoExist;
    }

    std::shared_ptr<FSNodeDir> m_rootNode = std::make_shared<FSNodeDir>("/");
};

}
#endif
