#ifndef PSEUDONIX_FILESYSTEM_MOUNT2_H
#define PSEUDONIX_FILESYSTEM_MOUNT2_H

#include <cassert>
#include <filesystem>
#include "generator.h"

namespace PseudoNix
{

enum FSResult2
{
    False,
    True,

    ErrorNotDirectory,
    ErrorNotFile,
    ErrorNotEmpty,
    ErrorReadOnly,
    ErrorExists,
    ErrorParentDoesNotExist,
    ErrorDoesNotExist,
    ErrorIsMounted,
    UnknownError = 255,
};

enum class NodeType2 {
    Unknown,
    MemFile,   // virtual file exists in memory only
    MemDir,    // virtual directory, exists in memory only
    MountFile, // file belongs to a mount
    MountDir,  // folder belongs to a mount
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
    virtual result_type remove(path_type relPath) = 0;
    virtual std::unique_ptr<std::streambuf> open(path_type relPath, std::ios::openmode mode) = 0;
    virtual NodeType2 getType(path_type relPath) const = 0;
    virtual bool is_read_only() const = 0;
    virtual Generator<path_type> list_dir(path_type relPath) = 0;

    virtual std::string get_info()
    {
        return "No Info";
    }
};

}

#endif
