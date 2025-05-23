#ifndef PSEUDONIX_FileSystem_H
#define PSEUDONIX_FileSystem_H

#include <map>
#include <string>
#include <cassert>
#include <filesystem>
#include <vector>
#include "FileSystemMount.h"
#include "FileSystemHelpers.h"
#include <format>

namespace PseudoNix
{

class VectorBackedStreamBuf : public std::streambuf {
public:
    explicit VectorBackedStreamBuf(std::vector<char>& buffer, std::ios::openmode _mode)
        : buffer_(buffer),
          m_mode(_mode)
    {
        if( _mode & std::ios::app )
        {
            setg(buffer_.data(), buffer_.data(), buffer_.data() + buffer_.size());
            setp(nullptr, nullptr);
        }
        else
        {
            setg(buffer_.data(), buffer_.data(), buffer_.data() + buffer_.size());
            setp(buffer_.data(), buffer_.data() + buffer_.size());
            //std::cout <<std::format("total size: {}   Write pos: {}", std::distance(pbase(), epptr()), std::distance(pbase(), pptr())) << std::endl;
        }
    }

    ~VectorBackedStreamBuf()
    {
        this->sync();
    }
    // Ensure output expansion if writing past current capacity
    int_type overflow(int_type ch) override {
        //std::cout <<std::format("writing: {},  total size: {}   Write pos: {}", static_cast<char>(ch), std::distance(pbase(), epptr()), std::distance(pbase(), pptr())) << std::endl;
        buffer_.push_back(static_cast<char>(ch));
        auto s = buffer_.size();
        buffer_.resize(buffer_.size() + 5);
        setp(buffer_.data() + s, buffer_.data() + buffer_.size());

        //buffer_.resize( buffer_.size() + 10);
        //char_type*
        //pbase() const { return _M_out_beg; }
        //
        //char_type*
        //pptr() const { return _M_out_cur; }
        //
        //char_type*
        //epptr() const { return _M_out_end; }
        return ch;
    }

    // Called when input buffer is exhausted
    int_type underflow() override {
        if (gptr() >= egptr()) return traits_type::eof();
        return traits_type::to_int_type(*gptr());
    }

    // Sync not needed in memory buffer, but we can update get area
    int sync() override {
        if( m_mode & std::ios::in )
            return 0;
        auto shortenBy = std::distance(pbase(), epptr()) - std::distance(pbase(), pptr());
        //std::cout << shortenBy << std::endl;
        //std::cout <<std::format("total size: {}   Write pos: {}", std::distance(pbase(), epptr()), std::distance(pbase(), pptr())) << std::endl;
        buffer_.resize(buffer_.size()-static_cast<size_t>(shortenBy));
        setp(nullptr, nullptr);
        //std::cout << "Sync" << std::endl;
        return 0;
    }

private:
    std::vector<char>& buffer_;
    std::ios::openmode m_mode;
};

class FileStream : public std::iostream {
public:
    FileStream() : std::iostream(nullptr)
    {
    }

    explicit FileStream(std::unique_ptr<std::streambuf> && stream_buff) : std::iostream(stream_buff.get()),
        _streamBuf(std::move(stream_buff))
    {
    }
private:
    std::unique_ptr<std::streambuf> _streamBuf;
};

class oFileStream : public std::ostream {
public:
    oFileStream() : std::ostream(nullptr)
    {
    }

    explicit oFileStream(std::unique_ptr<std::streambuf> && stream_buff) : std::ostream(stream_buff.get()),
        _streamBuf(std::move(stream_buff))
    {
    }
private:
    std::unique_ptr<std::streambuf> _streamBuf;
};

class iFileStream : public std::istream {
public:
    iFileStream() : std::istream(nullptr)
    {
    }

    explicit iFileStream(std::unique_ptr<std::streambuf> && stream_buff) : std::istream(stream_buff.get()),
        _streamBuf(std::move(stream_buff))
    {
    }
private:
    std::unique_ptr<std::streambuf> _streamBuf;
};

struct FSNode
{
    using path_type = std::filesystem::path;
    using result_type = FSResult;
    std::string  name;
    bool read_only = false;
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

    FSNodeFile(std::string _name) : FSNode(_name)
    {
    }
};

struct FSNodeDir : public FSNode
{
    std::map<std::string, std::shared_ptr<FSNode> > nodes;

    // a mount node
    std::shared_ptr<FSMountBase> mount;

    FSNodeDir(std::string _name) : FSNode(_name)
    {
    }
};


struct FileSystem;
struct NodeRef
{
    FSNode::path_type absPath;
    FileSystem *fs;

    operator std::string() const;
};


struct FileSystem
{
    using path_type   = FSNode::path_type;
    using result_type = FSNode::result_type;

    result_type is_read_only(path_type abs_path) const
    {
        if(abs_path == "/")
        {
            return m_rootNode->read_only ? result_type::True : result_type::False;
        }

        auto [mnt, rem ] = find_last_valid_virtual_node(abs_path);
        bool read_only = mnt->read_only;

        // if any of the parent paths are read only.
        // then this node is readonly
        read_only |= is_read_only(abs_path.parent_path()) == result_type::True;
        if(read_only)
            return result_type::True;

        if(auto d = std::dynamic_pointer_cast<FSNodeDir const>(mnt))
        {
            if(d->mount)
            {
                read_only |= d->mount->is_read_only();
            }
        }
        return read_only ? result_type::True : result_type::False;
    }

    result_type set_read_only(path_type abs_path, bool read_only)
    {
        auto [mnt, rem] = find_last_valid_virtual_node(abs_path);
        if(rem.empty())
        {
            mnt->read_only = read_only;
            return result_type::True;
        }
        return result_type::ErrorIsMounted;
    }
    /**
     * @brief exists
     * @param abs_path
     * @return
     *
     * Returns a result_type::True or result_type::False
     * if the path exists
     */
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

    /**
     * @brief mkdir
     * @param abs_path
     * @return
     *
     * Creates a directory and returns result_type::True
     * if it was successful
     */
    result_type mkdir(path_type abs_path)
    {
        _clean(abs_path);
        if(is_read_only(abs_path))
        {
            return result_type::ErrorReadOnly;
        }
        assert(abs_path.has_root_directory());

        auto [mnt, rem ] = find_last_valid_virtual_node(abs_path);

        if(!mnt)
            return result_type::UnknownError;

        if(rem.empty())
        {
            // location already exists
            return result_type::ErrorExists;
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
                    return result_type::ErrorParentDoesNotExist;
                }
                d->nodes[rem.generic_string()] = std::make_shared<FSNodeDir>(rem.generic_string());
                return result_type::True;
            }
        }
        return result_type::False;
    }

    /**
     * @brief mkfile
     * @param abs_path
     * @return
     *
     * Creates an empty file and returns result_type::True
     * if successful
     */
    result_type mkfile(path_type abs_path)
    {
        _clean(abs_path);
        if(is_read_only(abs_path))
        {
            return result_type::ErrorReadOnly;
        }

        assert(abs_path.has_root_directory());

        auto rel_path_to_root = abs_path.relative_path();

        auto [mnt, rem] = find_last_valid_virtual_node(abs_path);

        if(!mnt)
            return result_type::UnknownError;

        if(rem.empty())
        {
            // location already exists
            return result_type::ErrorExists;
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
                    return result_type::ErrorParentDoesNotExist;
                }

                d->nodes[rem.generic_string()] = std::make_shared<FSNodeFile>(rem.generic_string());
                return result_type::True;
            }
        }
        return result_type::UnknownError;
    }

    /**
     * @brief rm
     * @param abs_path
     * @return
     *
     * Removes a file and returns result_type::True if successful
     */
    result_type remove(path_type abs_path)
    {
        _clean(abs_path);
        assert(abs_path.has_root_directory());

        if(is_read_only(abs_path))
        {
            return result_type::ErrorReadOnly;
        }

        auto [mnt, rem] = find_last_valid_virtual_node(abs_path.parent_path());

        auto d = std::dynamic_pointer_cast<FSNodeDir>(mnt);
        if(d)
        {
            if(d->read_only)
                return result_type::ErrorReadOnly;
            if(d->mount)
            {
                return d->mount->remove(rem / abs_path.filename());
            }
            else
            {
                auto it = d->nodes.find(abs_path.filename().generic_string());
                if(it != d->nodes.end())
                {
                    if(auto cp = std::dynamic_pointer_cast<FSNodeDir>(it->second) )
                    {
                        if(!cp->nodes.empty())
                            return result_type::ErrorNotEmpty;
                        if(cp->mount)
                            return result_type::ErrorReadOnly;
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
                return result_type::ErrorDoesNotExist;
            }
        }
        return result_type::UnknownError;
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
                return result_type::ErrorNotDirectory;
            }

            // nothing mounted
            if(!dir->mount)
                return result_type::False;

            dir->mount = {};
            return result_type::True;
        }
        return result_type::UnknownError;
    }

    /**
     * @brief mount
     * @param abs_path_in_vfs
     * @param __args
     * @return
     *
     * Mounts a virtual file system inside the virtual filesystem
     *
     * A simple example:
     *
     * // Mounts the user's home directory on /mnt
     * mount<HostMount>("/mnt", "/home/user");
     */
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
        return const_cast<FileSystem*>(this)->find_last_valid_virtual_node(abs_path);
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

            auto it = r->nodes.find(first.generic_string());
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

    /**
     * @brief move
     * @param srcAbsPath
     * @param dstAbsPath
     * @return
     *
     * Moves a file/folder from one location to another
     */
    result_type move(path_type srcAbsPath, path_type dstAbsPath)
    {
        if(!exists(srcAbsPath))
        {
            // src file doesn't exist
            return result_type::ErrorDoesNotExist;
        }
        if(!exists(dstAbsPath.parent_path()))
        {
            // parent folder of dst doesnt exist
            return result_type::ErrorParentDoesNotExist;
        }

        auto tSrc = getType(srcAbsPath);
        auto tDst = getType(dstAbsPath);

        if( tDst == NodeType::MemDir || tDst == NodeType::MountDir)
            dstAbsPath = dstAbsPath / srcAbsPath.filename();

        auto tDstFolder = getType(dstAbsPath.parent_path());

        if(tSrc == NodeType::MemFile && tDstFolder == NodeType::MemDir )
        {
            auto [srcMnt, srcRem ] = find_last_valid_virtual_node(srcAbsPath);
            auto [dstMnt, dstRem ] = find_last_valid_virtual_node(dstAbsPath.parent_path());

            auto srcFile_p = std::dynamic_pointer_cast<FSNodeFile>(srcMnt);
            auto dstDir_p  = std::dynamic_pointer_cast<FSNodeDir>(dstMnt);

            dstDir_p->nodes[dstAbsPath.filename().generic_string()] = srcFile_p;

            remove(srcAbsPath);

            return result_type::True;
        }
        else if(tSrc == NodeType::MemDir && tDstFolder == NodeType::MemDir )
        {
            auto [srcMnt, srcRem ] = find_last_valid_virtual_node(srcAbsPath);
            auto [dstMnt, dstRem ] = find_last_valid_virtual_node(dstAbsPath.parent_path());

            auto srcDir_p = std::dynamic_pointer_cast<FSNodeDir>(srcMnt);
            auto dstDir_p  = std::dynamic_pointer_cast<FSNodeDir>(dstMnt);

            dstDir_p->nodes[dstAbsPath.filename().generic_string()] = srcDir_p;

            {
                auto [srcParent, srcParentRem ] = find_last_valid_virtual_node(srcAbsPath.parent_path());
                assert(srcParentRem.empty());
                auto srcParent_p = std::dynamic_pointer_cast<FSNodeDir>(srcParent);
                assert(srcParent_p);
                srcParent_p->nodes.erase(srcAbsPath.filename().generic_string());
            }

            return result_type::True;
        }
        else
        {
            // do the long way around, copy+delete
            auto ret = copy(srcAbsPath, dstAbsPath);
            if(ret != result_type::True)
                return ret;
            ret = remove(srcAbsPath);
            if(ret != result_type::True)
                return ret;
            return result_type::True;
        }
        return result_type::False;
    }

    /**
     * @brief copy
     * @param srcAbsPath
     * @param dstAbsPath
     * @return
     *
     * Copy a file from one location to another
     */
    result_type copy(path_type  srcAbsPath, path_type  dstAbsPath)
    {
        if(!exists(srcAbsPath))
        {
            // src file doesn't exist
            return result_type::ErrorDoesNotExist;
        }

        auto dType = getType(dstAbsPath);
        if(dType == NodeType::MemDir || dType == NodeType::MountDir)
        {
            dstAbsPath = dstAbsPath / srcAbsPath.filename();
        }

        // need to check that dstAbsPath.parent is a folder
        // and is writable
        {
            auto parentType = getType(dstAbsPath.parent_path());
            if(parentType == NodeType::MemDir)
            {
                auto [pN,rem2] = find_last_valid_virtual_node(dstAbsPath);
                assert(pN);
                if(pN->read_only)
                    return result_type::ErrorReadOnly;
            }
            if(parentType == NodeType::MountDir)
            {
                auto [pN,rem2] = find_last_valid_virtual_node(dstAbsPath);
                assert(pN);
                if(pN->read_only || std::dynamic_pointer_cast<FSNodeDir>(pN)->mount->is_read_only())
                    return result_type::ErrorReadOnly;
            }
        }
        if(!exists(dstAbsPath))
        {
            auto v = mkfile(dstAbsPath);
            if(v != result_type::True)
                return result_type::False; // cannot create dst file
        }

        auto Fout = this->openWrite(dstAbsPath, false);
        auto Fin  = this->openRead(srcAbsPath);

        if(!Fout.good() )
            return result_type::UnknownError;
        if(!Fin.good() )
            return result_type::UnknownError;

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

    oFileStream openWrite(path_type abs_path, bool append)
    {
        auto openMode = std::ios::out;
        if(append)
            openMode |= std::ios::app;
        return open_t<oFileStream>(abs_path, openMode);
    }

    iFileStream openRead(path_type abs_path)
    {
        return open_t<iFileStream>(abs_path, std::ios::in);
    }

    NodeType getType(path_type absPath) const
    {
        auto [mnt, rem] = find_last_valid_virtual_node(absPath);

        if(rem.empty())
        {
            if(auto d = std::dynamic_pointer_cast<const FSNodeDir>(mnt))
            {
                if(d->mount)
                    return d->mount->getType(rem);
                return NodeType::MemDir;
            }
            return NodeType::MemFile;
        }
        if(auto d = std::dynamic_pointer_cast<const FSNodeDir>(mnt))
        {
            if(d->mount)
                return d->mount->getType(rem);
        }
        return NodeType::NoExist;
    }

    Generator<path_type> list_dir(path_type absPath)
    {
        auto [mnt, rem] = find_last_valid_virtual_node(absPath);

        if(auto d = std::dynamic_pointer_cast<FSNodeDir>(mnt))
        {
            if(d->mount)
            {
                auto gen = d->mount->list_dir(rem);
                for(auto n : gen)
                {
                    co_yield n;
                }
            }
            else
            {
                for(auto & [name, n] : d->nodes)
                {
                    co_yield name;
                }
            }
        }
    }

    /**
     * @brief list_nodes_recursive
     * @param absPath
     * @return
     *
     * Return a generator that gernates a list of all virtual filessystem
     * nodes. Mounted files/folders are not returned
     */
    Generator<path_type> list_nodes_recursive(path_type absPath) const
    {
        auto [mnt, rem] = find_last_valid_virtual_node(absPath);
        if(!rem.empty())
            co_return;

        if(auto d = std::dynamic_pointer_cast<FSNodeDir const>(mnt))
        {
            for(auto & n : d->nodes)
            {
                co_yield absPath / n.first;
                for(auto cc : list_nodes_recursive(absPath / n.first))
                {
                    co_yield cc;
                }
            }
        }

    }

    NodeRef fs(path_type absPath)
    {
        _clean(absPath);
        return NodeRef{absPath, this};
    }

    /**
     * @brief getVirtualFileData
     * @param absPath
     * @return
     *
     * Returns a pointer to the vector of data for the virtual
     * file, if it exists. If it doesn't. nullptr is returned
     */
    std::vector<char>* getVirtualFileData(path_type absPath)
    {
        auto [mnt, rem] = find_last_valid_virtual_node(absPath);
        if(!rem.empty())
            return {};
        auto f = std::dynamic_pointer_cast<FSNodeFile>(mnt);
        if(!f)
            return {};

        return &f->data;
    }

    std::shared_ptr<FSNodeDir> m_rootNode = std::make_shared<FSNodeDir>("/");

protected:
    template<typename T>
    T open_t(path_type abs_path,  std::ios::openmode openmode)
    {
        _clean(abs_path);
        assert(abs_path.has_root_directory());
        auto rel_path_to_root = abs_path.relative_path();

        auto [mnt, rem ] = find_last_valid_virtual_node(abs_path);

        if(rem.empty())
        {
            if(auto f = std::dynamic_pointer_cast<FSNodeFile>(mnt))
            {
                auto bff = std::make_unique<VectorBackedStreamBuf>(f->data, openmode);
                return T(std::move(bff));
            }
            // an empty folder cannot open
            return T();
        }
        else
        {
            if(auto d = std::dynamic_pointer_cast<FSNodeDir>(mnt))
            {
                if(d->mount)
                {
                    auto bff = d->mount->open(rem, openmode);
                    return T(std::move(bff));
                }
            }
        }
        return {};
    }
};

inline PseudoNix::NodeRef::operator std::string() const
{
    auto out = fs->openRead(absPath);
    if(!out.good())
        return {};

    std::stringstream buffer;
    buffer << out.rdbuf();
    return buffer.str();
}

}

//
// Read from a filesystem node and append to a string:
//
//  std::string;
//  fs("/path/to/file") >> mystring;
//
inline void operator >> (PseudoNix::NodeRef  nodeleft, std::string & right)
{
    auto in = nodeleft.fs->openRead(nodeleft.absPath);
    if(!in.good())
        return;

    std::stringstream buffer;
    buffer << in.rdbuf();
    right += buffer.str();
}

inline void operator << (PseudoNix::NodeRef left, std::string_view right)
{
    //
    // F.fs("/path/to/my/file.txt") << "hello world";
    //

    (void)left;
    (void)right;
    auto out = left.fs->openWrite(left.absPath, true);
    if(!out.good())
        return;
    out << right;
}

inline void operator << (PseudoNix::NodeRef left, std::vector<uint8_t> const &right)
{
    //
    // F.fs("/path/to/my/file.txt") << "hello world";
    //

    (void)left;
    (void)right;
    auto out = left.fs->openWrite(left.absPath, true);
    if(!out.good())
        return;
    out.write( static_cast<char const*>(static_cast<void const*>(right.data())), static_cast<std::streamsize>(right.size()));
}



#endif
