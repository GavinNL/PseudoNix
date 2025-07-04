#ifndef PSEUDONIX_FILESYSTEM_MOUNT2_H
#define PSEUDONIX_FILESYSTEM_MOUNT2_H

#include <cassert>
#include <filesystem>
#include "generator.h"

namespace PseudoNix
{

enum FSResult
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

enum class NodeType {
    Unknown,
    MemFile,   // virtual file exists in memory only
    MemDir,    // virtual directory, exists in memory only
    MountFile, // file belongs to a mount
    MountDir,  // folder belongs to a mount
    Custom,    // Node that holds custom data
               // in the form of a std::any
    NoExist
};

struct FSMountBase
{
    using path_type = std::filesystem::path;
    using result_type = FSResult;
    using node_type = NodeType;

    virtual ~FSMountBase()
    {

    }

    /**
     * @brief exists
     * @param relPath
     * @return
     *
     * Return result_type::True if the relPath exists
     * inside this mount, result_type::False if it doesnt.
     *
     * valid retun types: result_type::Trye, result_type::False;
     */
    virtual result_type exists(path_type relPath) const = 0;
    virtual result_type mkdir(path_type relPath) = 0;
    virtual result_type mkfile(path_type relPath) = 0;
    virtual result_type remove(path_type relPath) = 0;
    virtual std::unique_ptr<std::streambuf> open(path_type relPath, std::ios::openmode mode) = 0;

    /**
     * @brief getType
     * @param relPath
     * @return
     *
     * Return NodeType::MountDir or NodeType::MountFile
     * or NodeType::NoExist
     */
    virtual NodeType getType(path_type relPath) const = 0;
    virtual bool is_read_only() const = 0;
    virtual Generator<path_type> list_dir(path_type relPath) = 0;

    virtual std::string get_info()
    {
        return "No Info";
    }
};

}

#endif
