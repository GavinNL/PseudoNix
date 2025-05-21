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
//#include "FileSystem.h"
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
        //auto pstr = path.generic_string();
        //file = std::fopen(pstr.c_str(), cmode.c_str());
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

    explicit FileStream(std::unique_ptr<std::streambuf> && stream_buff) : std::iostream(nullptr)
    {
        this->rdbuf(stream_buff.get());
        _streamBuf = std::move(stream_buff);
    }
private:
    std::unique_ptr<std::streambuf> _streamBuf;
};

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

    virtual Generator<path_type> list_dir(path_type relPath) = 0;
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

    virtual Generator<path_type> list_dir(path_type relPath) override
    {
        namespace fs = std::filesystem;
        auto abs_path = m_path_on_host / relPath;
        for (const auto& entry : fs::directory_iterator(abs_path)) {
            co_yield entry.path().lexically_proximate(abs_path);
        }
    }
};

struct FileSystem2;
struct NodeRef
{
    FSNode::path_type absPath;
    FileSystem2 *fs;
};

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
        else if(tSrc == NodeType2::MemDir && tDstFolder == NodeType2::MemDir )
        {
            auto [srcMnt, srcRem ] = find_last_valid_virtual_node(srcAbsPath);
            auto [dstMnt, dstRem ] = find_last_valid_virtual_node(dstAbsPath.parent_path());

            auto srcDir_p = std::dynamic_pointer_cast<FSNodeDir>(srcMnt);
            auto dstDir_p  = std::dynamic_pointer_cast<FSNodeDir>(dstMnt);

            dstDir_p->nodes[dstAbsPath.filename()] = srcDir_p;

            {
                auto [srcParent, srcParentRem ] = find_last_valid_virtual_node(srcAbsPath.parent_path());
                assert(srcParentRem.empty());
                auto srcParent_p = std::dynamic_pointer_cast<FSNodeDir>(srcParent);
                assert(srcParent_p);
                srcParent_p->nodes.erase(srcAbsPath.filename());
            }

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

    NodeRef fs(path_type absPath)
    {
        _clean(absPath);
        return NodeRef{absPath, this};

    }
    std::shared_ptr<FSNodeDir> m_rootNode = std::make_shared<FSNodeDir>("/");
};

}

void operator << (PseudoNix::NodeRef left, std::string_view right)
{
    (void)left;
    (void)right;
    auto out = left.fs->open(left.absPath, std::ios::out);
    if(!out.good())
        return;
    out << right;
}

#endif
