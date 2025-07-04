#ifndef PSEUDONIX_FileSystem_H
#define PSEUDONIX_FileSystem_H

#include <functional>
#include <map>
#include <string>
#include <cassert>
#include <filesystem>
#include <vector>
#include "FileSystemMount.h"
#include "FileSystemHelpers.h"
#include <any>
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

    std::vector<char> to_data()
    {
        if(!this->good())
            return {};

        std::vector<char> data;
        while(!eof())
        {
            auto c = get();
            data.push_back( static_cast<char>(c) );
        }
        return data;
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
    using data_container_type = std::vector<char>;
    data_container_type data;
    std::any            custom;
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


/**
 * @brief The NodeRef class
 *
 * This is a reference to a node in the
 * filesystem. It should be used for all interaction
 * with files.
 */
template<bool is_const>
struct NodeRef_t
{
public:
    FSNode::path_type absPath;     // the absolute path to the file
    std::conditional_t<is_const, FileSystem const*, FileSystem*> fs;
    std::conditional_t<is_const, std::shared_ptr<const FSNode>, std::shared_ptr<FSNode>> node;
    FSNode::path_type remPath;     // the remaining path relative to the mount

    using fs_type = decltype(fs);
    using node_type = decltype(node);
    using path_type = typename FSNode::path_type;

public:
    NodeRef_t(FSNode::path_type absPath_,
              fs_type fs_,
              node_type node_,
              FSNode::path_type remPath_) : absPath(absPath_),
                                          fs(fs_),
                                          node(node_),
                                          remPath(remPath_) {}

    operator std::string() const;

    bool exists() const
    {
        if(!node)
            return false;

        auto &mnt = node;
        auto &rem = remPath;
        //auto [mnt, rem ] = find_last_valid_virtual_node(abs_path);
        if(mnt && rem.empty())
        {
            return true;
        }

        auto d = std::dynamic_pointer_cast<FSNodeDir>(mnt);
        if(d)
        {
            if(d->mount)
            {
                return d->mount->exists(rem);
            }
            return false;
        }
        else
        {
            return false;
            // its a file for some reason?
        }
        return true;
    }
    FileSystem* filesystem() {
        return fs;
    }
    FSNode::path_type const& path() const
    {
        return absPath;
    }
    FSNodeFile::data_container_type* get_virtual_file_data()
    {
        if(auto f = file_node())
        {
            return &f->data;
        }
        return nullptr;
    }
    FSNodeFile::data_container_type* get_virtual_file_data() const
    {
        if(auto f = file_node())
        {
            return &f->data;
        }
        return nullptr;
    }

    bool is_file() const
    {
        if(auto d = file_node())
        {
            if(!remPath.empty())
                return false;

            return !d->custom.has_value() ? true : false;
        }
        if(auto d = std::dynamic_pointer_cast<FSNodeDir>(node))
        {
            return !d->mount ? false : (d->mount->getType(remPath)==NodeType::MountFile);
        }
        return false;
    }
    bool is_dir() const
    {
        if(auto f = file_node())
        {
            return false;
        }
        if(auto d = dir_node())
        {
            if(d->mount)
            {
                return d->mount->getType(remPath) == NodeType::MountDir;
            }
            else
            {
                return remPath.empty();
            }
            //return !d->mount ? true : (d->mount->getType(remPath)==NodeType::MountDir);
        }
        return false;
    }

    /**
     * @brief is_custom
     * @return
     *
     * returns true if it is a custom file. A filenode is custom
     * if its std::any contains a value
     */
    bool is_custom() const
    {
        if(auto d = file_node())
        {
            return d->custom.has_value() ? true : false;
        }
        return false;
    }

    /**
     * @brief is_mount
     * @return
     *
     * Returns true if this specific node is a mounted
     */
    bool is_mount_point() const
    {
        if(!remPath.empty())
            return false;

        if(auto d = dir_node())
        {
            return d->mount ? true : false;
        }
        return false;
    }
    /**
     * @brief is_mounted
     * @return
     *
     * Returns true if the absolute path refers to a
     * file/directory that is part of a mount
     *
     * This will return true if the file does not exist
     * within the mounted location, but one of its
     * parent folders is a mount_point
     */
    bool is_mounted() const
    {
        if(auto f = file_node())
            return false;

        //if(!remPath.empty())
        //    return true;

        if(auto d = dir_node())
        {
            return d->mount ? true : false;
        }
        return false;
    }

    auto file_node()
    {
        return std::dynamic_pointer_cast<FSNodeFile>(node);
    }
    auto file_node() const
    {
        return std::dynamic_pointer_cast<const FSNodeFile>(node);
    }
    auto dir_node()
    {
        return std::dynamic_pointer_cast<FSNodeDir>(node);
    }
    auto dir_node() const
    {
        return std::dynamic_pointer_cast<const FSNodeDir>(node);
    }
    NodeType get_type() const
    {
        if(remPath.empty())
        {
            if(auto d = dir_node())
            {
                if(d->mount)
                    return d->mount->getType(remPath);
                return NodeType::MemDir;
            }
            if(auto f = file_node())
            {
                if(f->custom.has_value())
                    return NodeType::Custom;
                return NodeType::MemFile;
            }
        }
        if(auto d = dir_node())
        {
            if(d->mount)
                return d->mount->getType(remPath);
        }
        return NodeType::NoExist;
    }

    Generator<FSNode::path_type> list_dir() const
    {
        if(auto d = dir_node())
        {
            if(d->mount)
            {
                auto gen = d->mount->list_dir(remPath);
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
    friend struct FileSystem;
};

using NodeRef = NodeRef_t<false>;
using ConstNodeRef = NodeRef_t<true>;

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

#if 1
        auto ref = fs(abs_path);
        auto mnt = ref.node;
        auto rem = ref.remPath;
#else
        auto [mnt, rem ] = find_last_valid_virtual_node(abs_path);
#endif
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
        auto ref = fs(abs_path);
        if(ref.remPath.empty())
        {
            ref.node->read_only = read_only;
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
        auto ref = fs(abs_path);
        return ref.exists() ? result_type::True : result_type::False;
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

    result_type mkdirs(path_type abs_path)
    {
        auto parent_type = getType(abs_path.parent_path());
        if(parent_type == NodeType::NoExist)
        {
            mkdirs(abs_path.parent_path());
        }
        return mkdir(abs_path);
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

    template<typename _Tp, typename... _Args>
    result_type mkcustom(path_type abs_path_in_vfs, _Args&&... __args)
    {
        auto p = fs(abs_path_in_vfs.parent_path());
        if( p.is_mounted())
            return result_type::ErrorIsMounted;

        // Parent folder doesnt exist
        // so try to recursively created it
        if(!p.exists())
        {
            auto dd = mkdirs(abs_path_in_vfs.parent_path());
            if(dd != result_type::True)
                return dd;
        }

        auto fsucc = mkfile(abs_path_in_vfs);
        if(fsucc != result_type::True)
            return fsucc;

        auto fref = fs(abs_path_in_vfs);
        auto fn = fref.file_node();
        fn->custom.emplace<_Tp>(std::forward<_Args>(__args)...);

        return result_type::True;
    }

    template<typename Tp>
    Tp* getCustom(path_type absPath)
    {
        auto f = fs(absPath);
        if(f.is_mounted())
            return nullptr;
        if(!f.remPath.empty())
            return nullptr;
        if(f.is_custom())
        {
            auto fn = f.file_node();
            if(fn)
            {
                auto ptr = std::any_cast<Tp>(&fn->custom);
                return ptr;
            }
        }
        return nullptr;
    }


    template<typename Tp>
    Tp& mkcustom_or_get(path_type absPath, Tp const &defaultVal)
    {
        Tp* c = getCustom<Tp>(absPath);
        if(!c)
        {
            auto r = mkcustom<Tp>(absPath, defaultVal);
            if(r != result_type::True)
                throw std::runtime_error(std::format("Error creating custom file at {}", absPath.generic_string()));
            c = getCustom<Tp>(absPath);
        }
        return *c;
    }

    template<typename T>
    result_type setCustom(path_type absPath, T const& value)
    {
        auto p = getCustom<T>(absPath);
        if(!p)
        {
            auto r = mkcustom<T>(absPath, value);
            if(r != result_type::True)
            {
                return r;
            }
            return r;
        }
        *p = value;
        return result_type::True;
    }

    std::function<void(path_type)> m_onRemoveFile;
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
                        if(m_onRemoveFile)
                            m_onRemoveFile(abs_path);
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
        auto ref = fs(abs_path_in_vfs);

        if(!ref.is_mount_point())
        {
            return result_type::False;
        }

        auto dir = ref.dir_node();
        if(!dir)
            return result_type::ErrorNotDirectory;

        if(!dir->mount)
            return result_type::False;

        dir->mount = {};

        return result_type::True;
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
        auto ref = fs(abs_path_in_vfs);

        if(!ref.is_dir() || ref.is_mounted())
            return result_type::ErrorNotDirectory;

        auto dir = ref.dir_node();

        if(dir->mount)
            return result_type::False;

        dir->mount = std::make_shared<_Tp>(std::forward<_Args>(__args)...);
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
        auto ref = fs(absPath);
        return ref.get_type();
    }

    Generator<path_type> list_dir(path_type absPath) const
    {
        auto ref = fs(absPath);
        for(auto n : ref.list_dir())
        {
            co_yield n;
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
    Generator<path_type> list_dir_recursive(path_type absPath) const
    {
        auto ref = fs(absPath);
        for(auto n : ref.list_dir())
        {
            co_yield  n;

            for(auto c : list_dir_recursive(absPath/n))
            {
                co_yield n / c;
            }
        }
    }
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
        auto [mnt, rem] = find_last_valid_virtual_node(absPath);
        return NodeRef{absPath, this, mnt, rem};
    }
    ConstNodeRef fs(path_type absPath) const
    {
        _clean(absPath);
        auto [mnt, rem] = find_last_valid_virtual_node(absPath);
        return ConstNodeRef{absPath, this, mnt, rem};
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

template<>
inline PseudoNix::NodeRef_t<false>::operator std::string() const
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
    auto in = nodeleft.filesystem()->openRead(nodeleft.path());
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
    auto out = left.filesystem()->openWrite(left.path(), true);
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
    auto out = left.filesystem()->openWrite(left.path(), true);
    if(!out.good())
        return;
    out.write( static_cast<char const*>(static_cast<void const*>(right.data())), static_cast<std::streamsize>(right.size()));
}



#endif
