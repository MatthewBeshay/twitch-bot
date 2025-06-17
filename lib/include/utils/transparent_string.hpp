#pragma once

// C++ Standard Library
#include <concepts>
#include <functional>
#include <string>
#include <string_view>

/// Hash for unordered containers supporting heterogeneous lookup on string-like keys.
struct TransparentStringHash {
    using is_transparent = void;

    template <std::convertible_to<std::string_view> S>
    std::size_t operator()(const S& s) const noexcept
    {
        return std::hash<std::string_view>{}(std::string_view{s});
    }
};

/// Equality comparator enabling heterogeneous lookup on string-like keys.
struct TransparentStringEq {
    using is_transparent = void;

    template <std::convertible_to<std::string_view> A, std::convertible_to<std::string_view> B>
    bool operator()(const A& a, const B& b) const noexcept
    {
        return std::string_view{a} == std::string_view{b};
    }
};
