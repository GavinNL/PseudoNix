#ifndef PSEUDONIX_HELPERS_H
#define PSEUDONIX_HELPERS_H

#include <charconv>
#include <string_view>

namespace PseudoNix
{

template<typename number_t>
bool to_number(std::string_view v, number_t & value)
{
    return std::errc() == std::from_chars(v.begin(), v.end(), value).ec;
}

}

#endif
