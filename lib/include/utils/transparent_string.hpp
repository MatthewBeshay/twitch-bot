#pragma once

// C++ Standard Library
#include <concepts>
#include <functional>
#include <string>
#include <string_view>

/// Heterogeneous hash functor for any string-like type convertible to std::string_view.
/// Allows unordered_map to accept std::string_view, const char*, std::string, etc., without allocation.
struct TransparentStringHash {
    using is_transparent = void;

    template <std::convertible_to<std::string_view> S>
    std::size_t operator()(S const& s) const noexcept
    {
        // Normalize to string_view, then use std::hash<string_view>.
        return std::hash<std::string_view>{}(std::string_view(s));
    }
};

/// Heterogeneous equality comparator for any string-like type convertible to std::string_view.
/// Enables unordered_map to compare keys of different string-like types.
struct TransparentStringEq {
    using is_transparent = void;

    template <
        std::convertible_to<std::string_view> A,
        std::convertible_to<std::string_view> B
    >
    bool operator()(A const& a, B const& b) const noexcept
    {
        return std::string_view(a) == std::string_view(b);
    }
};
