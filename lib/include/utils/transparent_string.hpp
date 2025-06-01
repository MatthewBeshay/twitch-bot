#pragma once

#include <concepts>
#include <string>
#include <string_view>
#include <functional>

//----------------------------------------------------------------
// TransparentStringHash with C++20 concepts
//----------------------------------------------------------------
struct TransparentStringHash {
    using is_transparent = void;

    template<std::convertible_to<std::string_view> S>
    std::size_t operator()(S const& s) const noexcept {
        // We always normalize to a std::string_view, then hash.
        return std::hash<std::string_view>{}(std::string_view(s));
    }
};

//----------------------------------------------------------------
// TransparentStringEq with C++20 concepts
//----------------------------------------------------------------
struct TransparentStringEq {
    using is_transparent = void;

    template<
        std::convertible_to<std::string_view> A,
        std::convertible_to<std::string_view> B
    >
    bool operator()(A const& a, B const& b) const noexcept {
        return std::string_view(a) == std::string_view(b);
    }
};
