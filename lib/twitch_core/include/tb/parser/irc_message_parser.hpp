#pragma once

// C++ Standard Library
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

// Boost.Asio
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

// GSL
#include <gsl/gsl>

// Project
#include "irc_simd_scan.hpp"
#include <tb/utils/attributes.hpp>

namespace twitch_bot {

/// Parsed IRC message - views into the original buffer, no ownership or allocations.
struct IrcMessage {
    static constexpr std::size_t max_params = 16; // hard limit on middle parameters

    std::string_view command; // IRC command (e.g. "PRIVMSG")
    std::array<std::string_view, max_params> params; // middle parameters

    // Using plain bytes instead of bitfields tends to generate simpler code in hot loops.
    uint8_t param_count = 0; // populated entries in params
    uint8_t is_moderator = 0; // tag "mod=1" present
    uint8_t is_broadcaster = 0; // broadcaster badge present

    std::string_view raw_tags; // full tag block (no leading '@')
    std::string_view prefix; // server or user prefix (no ':')
    std::string_view trailing; // text after the trailing ':'

    static constexpr std::size_t mod_tag_len = 5; // length of "mod=1"
    static constexpr std::size_t broadcaster_tag_len = 13; // length of "broadcaster/1"
    static constexpr std::size_t badges_prefix_len = 7; // length of "badges="

    /// Span of parameters (mutable when object is non-const).
    [[nodiscard]]
    TB_FORCE_INLINE auto parameters() noexcept -> gsl::span<std::string_view>
    {
        return {params.data(), params.data() + param_count};
    }

    /// Read-only span of parameters.
    [[nodiscard]]
    TB_FORCE_INLINE auto parameters() const noexcept -> gsl::span<const std::string_view>
    {
        return {params.data(), params.data() + param_count};
    }

    /// First tag value matching \p key, or empty if absent.
    /// Precondition: \p key is non-empty.
    [[nodiscard]]
    TB_FORCE_INLINE auto get_tag(std::string_view key) const noexcept -> std::string_view
    {
        Expects(!key.empty());

        const char* cursor = raw_tags.data();
        const char* const endp = cursor + raw_tags.size();
        const std::size_t key_len = key.size();

        while (cursor < endp) {
            // match "key="
            const std::size_t remaining = gsl::narrow_cast<std::size_t>(endp - cursor);
            if (remaining >= key_len + 1 && std::memcmp(cursor, key.data(), key_len) == 0
                && cursor[key_len] == '=') {
                const char* value_start = cursor + key_len + 1;
                const char* value_end
                    = static_cast<const char*>(std::memchr(value_start, ';', endp - value_start));
                if (!value_end) {
                    value_end = endp;
                }
                return {value_start, gsl::narrow_cast<std::size_t>(value_end - value_start)};
            }

            // advance to next tag
            const char* next_sep
                = static_cast<const char*>(std::memchr(cursor, ';', endp - cursor));
            if (next_sep) {
                cursor = next_sep + 1;
            } else {
                cursor = endp;
            }
        }

        return {};
    }
};

namespace detail {

    // Find first space or return endp. Uses the 64-byte scanner.
    TB_FORCE_INLINE const char* find_first_space_fast(const char* ptr, const char* endp) noexcept
    {
        // Small-range scalar fast path
        if (static_cast<size_t>(endp - ptr) < 16) {
            if (const void* p = std::memchr(ptr, ' ', static_cast<size_t>(endp - ptr))) {
                return static_cast<const char*>(p);
            }
            return endp;
        }

        const char* scan = ptr;
        while (scan < endp) {
            const size_t n = static_cast<size_t>(endp - scan);
            const size_t c = n < 64 ? n : size_t(64);
            auto masks = irc_simd::scan64(reinterpret_cast<const uint8_t*>(scan), c);
            if (masks.spaces) {
                return scan + irc_simd::ctz64(masks.spaces);
            }
            scan += c;
        }
        return endp;
    }

    // Split params by spaces and detect trailing when a token begins with ':'.
    TB_FORCE_INLINE void
    parse_params_and_trailing_fast(const char* ptr, const char* endp, IrcMessage& msg) noexcept
    {
        // Small-range scalar fast path
        if (static_cast<size_t>(endp - ptr) < 16) {
            const char* p = ptr;
            const char* token_start = ptr;
            while (p < endp) {
                char ch = *p++;
                if (ch == ' ' && msg.param_count < IrcMessage::max_params) {
                    if (*token_start == ':') {
                        const char* t = token_start + 1;
                        msg.trailing = {t, gsl::narrow_cast<std::size_t>(endp - t)};
                        return;
                    }
                    msg.params[msg.param_count++]
                        = {token_start, gsl::narrow_cast<std::size_t>(p - token_start - 1)};
                    token_start = p;
                }
            }
            if (token_start < endp && msg.param_count < IrcMessage::max_params) {
                if (*token_start == ':') {
                    const char* t = token_start + 1;
                    msg.trailing = {t, gsl::narrow_cast<std::size_t>(endp - t)};
                } else {
                    msg.params[msg.param_count++]
                        = {token_start, gsl::narrow_cast<std::size_t>(endp - token_start)};
                }
            }
            return;
        }

        const char* token_start = ptr;
        const char* scan = ptr;

        while (scan < endp) {
            const size_t n = static_cast<size_t>(endp - scan);
            const size_t c = n < 64 ? n : size_t(64);
            auto masks = irc_simd::scan64(reinterpret_cast<const uint8_t*>(scan), c);

            uint64_t sp = masks.spaces;
            while (sp && msg.param_count < IrcMessage::max_params) {
                const uint32_t off = irc_simd::pop_lowest(sp);
                const char* token_end = scan + off;

                if (token_start == token_end) {
                    msg.params[msg.param_count++] = {token_start, 0};
                    token_start = token_end + 1;
                } else if (*token_start == ':') {
                    const char* t = token_start + 1;
                    msg.trailing = {t, gsl::narrow_cast<std::size_t>(endp - t)};
                    return;
                } else {
                    msg.params[msg.param_count++]
                        = {token_start, gsl::narrow_cast<std::size_t>(token_end - token_start)};
                    token_start = token_end + 1;
                }
            }

            scan += c;
        }

        if (token_start < endp && msg.param_count < IrcMessage::max_params) {
            if (*token_start == ':') {
                const char* t = token_start + 1;
                msg.trailing = {t, gsl::narrow_cast<std::size_t>(endp - t)};
            } else {
                msg.params[msg.param_count++]
                    = {token_start, gsl::narrow_cast<std::size_t>(endp - token_start)};
            }
        }
    }

} // namespace detail

/// Parse one raw IRC line (no CRLF) into an IrcMessage.
/// All views refer to \p raw; no allocations.
/// Pre: raw.empty() || raw.data() != nullptr.
/// Post: result.param_count <= max_params.
[[nodiscard]]
TB_FORCE_INLINE auto parse_irc_line(std::string_view raw) noexcept -> IrcMessage
{
    Expects(raw.empty() || raw.data() != nullptr);

    IrcMessage msg{};
    const char* ptr = raw.data();
    const char* const endp = ptr + raw.size();

    // [1] tags block - SIMD fast path
    if (ptr < endp && *ptr == '@') {
        ++ptr;

        uint8_t mod = 0;
        uint8_t bc = 0;
        const char* tag_space = irc_simd::find_space_in_tags_and_flags(ptr, endp, mod, bc);

        msg.raw_tags = {ptr, gsl::narrow_cast<std::size_t>(tag_space - ptr)};
        msg.is_moderator = mod;
        msg.is_broadcaster = bc;

        if (tag_space == endp) {
            Ensures(msg.param_count <= IrcMessage::max_params);
            return msg; // no space - only tags present
        }
        ptr = tag_space + 1;
    }

    // [2] prefix
    if (ptr < endp && *ptr == ':') {
        ++ptr;
        const char* space_pos = detail::find_first_space_fast(ptr, endp);
        if (space_pos == endp) {
            msg.prefix = {ptr, gsl::narrow_cast<std::size_t>(endp - ptr)};
            Ensures(msg.param_count <= IrcMessage::max_params);
            return msg;
        }
        msg.prefix = {ptr, gsl::narrow_cast<std::size_t>(space_pos - ptr)};
        ptr = space_pos + 1;
    }

    // [3] command
    {
        const char* space_pos = detail::find_first_space_fast(ptr, endp);
        if (space_pos != endp) {
            msg.command = {ptr, gsl::narrow_cast<std::size_t>(space_pos - ptr)};
            ptr = space_pos + 1;
        } else {
            msg.command = {ptr, gsl::narrow_cast<std::size_t>(endp - ptr)};
            Ensures(msg.param_count <= IrcMessage::max_params);
            return msg;
        }
    }

    // [4] parameters and trailing
    detail::parse_params_and_trailing_fast(ptr, endp, msg);
    Ensures(msg.param_count <= IrcMessage::max_params);
    return msg;
}

} // namespace twitch_bot
