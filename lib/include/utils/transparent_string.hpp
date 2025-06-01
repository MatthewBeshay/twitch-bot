// utils/transparent_string.hpp
#pragma once

#include <string_view>
#include <functional>

/// A hash functor that can accept std::string, std::string_view, const char*, etc.
/// All of those implicitly convert to std::string_view.
struct TransparentStringHash {
    using is_transparent = void; // Enables heterogeneous lookup

    std::size_t operator()(std::string_view sv) const noexcept {
        return std::hash<std::string_view>{}(sv);
    }
};

/// An equality functor that can accept std::string, std::string_view, const char*, etc.
/// Again, they all convert to std::string_view.
struct TransparentStringEq {
    using is_transparent = void; // Enables heterogeneous lookup

    bool operator()(std::string_view a, std::string_view b) const noexcept {
        return a == b;
    }
};
