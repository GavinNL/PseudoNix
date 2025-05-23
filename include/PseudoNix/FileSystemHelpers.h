#ifndef PSEUDONIX_FILESYSTEM_HELPERS_H
#define PSEUDONIX_FILESYSTEM_HELPERS_H

#include <filesystem>

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


}

#endif
