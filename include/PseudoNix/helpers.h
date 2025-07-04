#ifndef PSEUDONIX_HELPERS_H
#define PSEUDONIX_HELPERS_H

#include <charconv>
#include <string_view>
#include <sstream>
#include <string>

namespace PseudoNix
{

template<typename number_t>
bool to_number(std::string_view v, number_t & value)
{
    if constexpr (requires { std::from_chars(v.begin(), v.end(), value); }) {
        return std::errc() == std::from_chars(v.begin(), v.end(), value).ec;
    }
    else
    {
        std::istringstream iss(std::string(v.begin(),v.end()));
        iss >> value;
        return !iss.fail() && iss.eof();
    }
}

/**
 * @brief splitVar
 * @param var_def
 * @return
 *
 * Given a stringview that looks like "VAR=VALUE", split this into two string views:
 * VAR and VALUE
 */
inline std::pair<std::string_view, std::string_view> splitVar(std::string_view var_def)
{
    auto i = var_def.find_first_of('=');
    if(i!=std::string::npos)
    {
        return std::pair(std::string_view(var_def.begin(),
                                          var_def.begin() + static_cast<std::ptrdiff_t>(i)),
                         std::string_view(var_def.begin() + static_cast<std::ptrdiff_t>(i) + 1,
                                          var_def.end()));
    }
    return {};
};


/**
 * @brief join
 * @param c
 * @param delimiter
 * @return
 *
 * Used to join a container for printing
 * std::format("{}", join(vector, ","));
 */
template <typename Container>
std::string join(const Container& c, const std::string& delimiter = ", ") {
    std::ostringstream oss;
    auto it = c.begin();
    if (it != c.end()) {
        oss << *it;
        ++it;
    }
    for (; it != c.end(); ++it) {
        oss << delimiter << *it;
    }
    return oss.str();
}

}

#endif
