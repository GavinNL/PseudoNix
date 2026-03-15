#ifndef PSEUDONIX_EXPECTED_H
#define PSEUDONIX_EXPECTED_H

#include <variant>

namespace PseudoNix
{

template<typename T, typename E>
struct Expected : private std::variant<T, E>
{
    using value_type   = T;
    using error_type   = E;
    using variant_type = std::variant<T, E>;

    Expected()
        : variant_type()
    {}

    Expected(value_type const &t)
        : variant_type(t) {};

    Expected(value_type &&t)
        : variant_type(std::move(t)) {};

    Expected(error_type const &t)
        : variant_type(t) {};

    Expected(error_type &&t)
        : variant_type(std::move(t)) {};

    bool operator==(error_type const &e) const
    {
        if (std::holds_alternative<error_type>(*this))
        {
            return std::get<error_type>(*this) == e;
        }
        return false;
    }
    bool operator==(value_type const &e) const
    {
        if (std::holds_alternative<value_type>(*this))
        {
            return std::get<value_type>(*this) == e;
        }
        return false;
    }
    bool has_value() const
    {
        return std::holds_alternative<value_type>(*this);
    }
    value_type &value()
    {
        return std::get<value_type>(*this);
    }
    value_type const &value() const
    {
        return std::get<value_type const>(*this);
    }
    error_type &error()
    {
        return std::get<value_type>(*this);
    }
    error_type const &error() const
    {
        return std::get<value_type const>(*this);
    }
};

} // namespace PseudoNix

#endif
