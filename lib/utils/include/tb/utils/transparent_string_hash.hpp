#pragma once

// C++ Standard Library
#include <concepts>
#include <cstddef>
#include <functional>
#include <string_view>
#include <type_traits>

// GSL
#include <gsl/gsl>

namespace tb::detail {

// True if T is (possibly cv/ref-qualified) pointer to CharT
template <class T, class CharT>
inline constexpr bool is_char_ptr_v = std::is_pointer_v<std::remove_cvref_t<T>>
    && std::same_as<std::remove_cv_t<std::remove_pointer_t<std::remove_cvref_t<T>>>, CharT>;

} // namespace tb::detail

template <class CharT = char, class Traits = std::char_traits<CharT>>
struct TransparentBasicStringHash {
    using is_transparent = void;

    template <std::convertible_to<std::basic_string_view<CharT, Traits>> S>
    std::size_t operator()(const S& s) const noexcept
    {
        if constexpr (tb::detail::is_char_ptr_v<S, CharT>) {
            Expects(s != nullptr); // avoid UB constructing string_view
        }
        using sv = std::basic_string_view<CharT, Traits>;
        return std::hash<sv>{}(sv{s});
    }
};

template <class CharT = char, class Traits = std::char_traits<CharT>>
struct TransparentBasicStringEq {
    using is_transparent = void;

    template <std::convertible_to<std::basic_string_view<CharT, Traits>> A,
              std::convertible_to<std::basic_string_view<CharT, Traits>> B>
    bool operator()(const A& a, const B& b) const noexcept
    {
        if constexpr (tb::detail::is_char_ptr_v<A, CharT>) {
            Expects(a != nullptr);
        }
        if constexpr (tb::detail::is_char_ptr_v<B, CharT>) {
            Expects(b != nullptr);
        }
        using sv = std::basic_string_view<CharT, Traits>;
        return sv{a} == sv{b};
    }
};

template <class CharT = char, class Traits = std::char_traits<CharT>>
struct TransparentBasicStringLess {
    using is_transparent = void;

    template <std::convertible_to<std::basic_string_view<CharT, Traits>> A,
              std::convertible_to<std::basic_string_view<CharT, Traits>> B>
    bool operator()(const A& a, const B& b) const noexcept
    {
        if constexpr (tb::detail::is_char_ptr_v<A, CharT>) {
            Expects(a != nullptr);
        }
        if constexpr (tb::detail::is_char_ptr_v<B, CharT>) {
            Expects(b != nullptr);
        }
        using sv = std::basic_string_view<CharT, Traits>;
        return sv{a} < sv{b};
    }
};
