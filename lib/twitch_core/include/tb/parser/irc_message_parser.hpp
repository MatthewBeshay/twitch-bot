/*
Module Name:
- irc_message_parser.hpp

Abstract:
- Zero-copy IRC line parser for hot paths.
- Produces views into the input buffer, avoids allocations, and uses a 64 byte SIMD scanner for separators.
- Detects Twitch-specific moderator and broadcaster signals from tags while scanning.
*/
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

// Core
#include "irc_simd_scan.hpp"
#include <tb/utils/attributes.hpp>

namespace twitch_bot
{

    // Parsed IRC message - views only, no ownership.
    struct IrcMessage
    {
        static constexpr std::size_t max_params = 16; // hard cap on middle params

        std::string_view command; // e.g. "PRIVMSG"
        std::array<std::string_view, max_params> params;

        // Use bytes instead of bitfields to keep generated code simple in hot loops.
        uint8_t param_count = 0;
        uint8_t is_moderator = 0; // tag "mod=1" or "user-type=mod"
        uint8_t is_broadcaster = 0; // badges contains "broadcaster/1"

        std::string_view raw_tags; // entire tag block without leading '@'
        std::string_view prefix; // server or user, without leading ':'
        std::string_view trailing; // text after the first leading ':' in the trailing field

        static constexpr std::size_t mod_tag_len = 5; // "mod=1"
        static constexpr std::size_t broadcaster_tag_len = 13; // "broadcaster/1"
        static constexpr std::size_t badges_prefix_len = 7; // "badges="

        // Params as a span for ergonomic iteration without copying.
        [[nodiscard]]
        TB_FORCE_INLINE auto parameters() noexcept -> gsl::span<std::string_view>
        {
            return { params.data(), params.data() + param_count };
        }

        [[nodiscard]]
        TB_FORCE_INLINE auto parameters() const noexcept -> gsl::span<const std::string_view>
        {
            return { params.data(), params.data() + param_count };
        }

        // Fast single-pass tag lookup to avoid allocating a map.
        // Pre: key is non-empty.
        [[nodiscard]]
        TB_FORCE_INLINE auto get_tag(std::string_view key) const noexcept -> std::string_view
        {
            Expects(!key.empty());

            const char* cursor = raw_tags.data();
            const char* const endp = cursor + raw_tags.size();
            const std::size_t key_len = key.size();

            while (cursor < endp)
            {
                // match "key="
                const std::size_t remaining = gsl::narrow_cast<std::size_t>(endp - cursor);
                if (remaining >= key_len + 1 && std::memcmp(cursor, key.data(), key_len) == 0 && cursor[key_len] == '=')
                {
                    const char* value_start = cursor + key_len + 1;
                    const char* value_end = static_cast<const char*>(std::memchr(value_start, ';', endp - value_start));
                    if (!value_end)
                    {
                        value_end = endp;
                    }
                    return { value_start, gsl::narrow_cast<std::size_t>(value_end - value_start) };
                }

                // advance to next tag
                const char* next_sep = static_cast<const char*>(std::memchr(cursor, ';', endp - cursor));
                cursor = next_sep ? next_sep + 1 : endp;
            }

            return {};
        }
    };

    namespace detail
    {

        // Find first space cheaply. SIMD over 64 byte blocks with a short scalar fast path.
        TB_FORCE_INLINE const char* find_first_space_fast(const char* ptr, const char* endp) noexcept
        {
            // Small input: scalar is cheaper than setting up SIMD.
            if (static_cast<size_t>(endp - ptr) < 16)
            {
                if (const void* p = std::memchr(ptr, ' ', static_cast<size_t>(endp - ptr)))
                {
                    return static_cast<const char*>(p);
                }
                return endp;
            }

            const char* scan = ptr;
            while (scan < endp)
            {
                const size_t n = static_cast<size_t>(endp - scan);
                const size_t c = n < 64 ? n : size_t(64);
                auto masks = irc_simd::scan64(reinterpret_cast<const uint8_t*>(scan), c);
                if (masks.spaces)
                {
                    return scan + irc_simd::ctz64(masks.spaces);
                }
                scan += c;
            }
            return endp;
        }

        // Split middle params by spaces and capture trailing when a token starts with ':'.
        // This keeps a single forward pass and avoids extra scans.
        TB_FORCE_INLINE void
        parse_params_and_trailing_fast(const char* ptr, const char* endp, IrcMessage& msg) noexcept
        {
            // Small input: scalar wins.
            if (static_cast<size_t>(endp - ptr) < 16)
            {
                const char* p = ptr;
                const char* token_start = ptr;
                while (p < endp)
                {
                    char ch = *p++;
                    if (ch == ' ' && msg.param_count < IrcMessage::max_params)
                    {
                        if (*token_start == ':')
                        {
                            const char* t = token_start + 1;
                            msg.trailing = { t, gsl::narrow_cast<std::size_t>(endp - t) };
                            return;
                        }
                        msg.params[msg.param_count++] = { token_start, gsl::narrow_cast<std::size_t>(p - token_start - 1) };
                        token_start = p;
                    }
                }
                if (token_start < endp && msg.param_count < IrcMessage::max_params)
                {
                    if (*token_start == ':')
                    {
                        const char* t = token_start + 1;
                        msg.trailing = { t, gsl::narrow_cast<std::size_t>(endp - t) };
                    }
                    else
                    {
                        msg.params[msg.param_count++] = { token_start, gsl::narrow_cast<std::size_t>(endp - token_start) };
                    }
                }
                return;
            }

            const char* token_start = ptr;
            const char* scan = ptr;

            while (scan < endp)
            {
                const size_t n = static_cast<size_t>(endp - scan);
                const size_t c = n < 64 ? n : size_t(64);
                auto masks = irc_simd::scan64(reinterpret_cast<const uint8_t*>(scan), c);

                uint64_t sp = masks.spaces;
                while (sp && msg.param_count < IrcMessage::max_params)
                {
                    const uint32_t off = irc_simd::pop_lowest(sp);
                    const char* token_end = scan + off;

                    if (token_start == token_end)
                    {
                        msg.params[msg.param_count++] = { token_start, 0 };
                        token_start = token_end + 1;
                    }
                    else if (*token_start == ':')
                    {
                        const char* t = token_start + 1;
                        msg.trailing = { t, gsl::narrow_cast<std::size_t>(endp - t) };
                        return;
                    }
                    else
                    {
                        msg.params[msg.param_count++] = { token_start, gsl::narrow_cast<std::size_t>(token_end - token_start) };
                        token_start = token_end + 1;
                    }
                }

                scan += c;
            }

            if (token_start < endp && msg.param_count < IrcMessage::max_params)
            {
                if (*token_start == ':')
                {
                    const char* t = token_start + 1;
                    msg.trailing = { t, gsl::narrow_cast<std::size_t>(endp - t) };
                }
                else
                {
                    msg.params[msg.param_count++] = { token_start, gsl::narrow_cast<std::size_t>(endp - token_start) };
                }
            }
        }

    } // namespace detail

    // Parse one raw IRC line (no CRLF) into an IrcMessage.
    // All views refer to 'raw'; no allocations.
    // Pre: raw.empty() || raw.data() is not null.
    // Post: param_count <= max_params.
    [[nodiscard]]
    TB_FORCE_INLINE auto parse_irc_line(std::string_view raw) noexcept -> IrcMessage
    {
        Expects(raw.empty() || raw.data() != nullptr);

        IrcMessage msg{};
        const char* ptr = raw.data();
        const char* const endp = ptr + raw.size();

        // [1] tags block - processed with a SIMD assisted scan that also finds mod and broadcaster.
        if (ptr < endp && *ptr == '@')
        {
            ++ptr;

            uint8_t mod = 0;
            uint8_t bc = 0;
            const char* tag_space = irc_simd::find_space_in_tags_and_flags(ptr, endp, mod, bc);

            msg.raw_tags = { ptr, gsl::narrow_cast<std::size_t>(tag_space - ptr) };
            msg.is_moderator = mod;
            msg.is_broadcaster = bc;

            if (tag_space == endp)
            {
                Ensures(msg.param_count <= IrcMessage::max_params);
                return msg; // only tags present
            }
            ptr = tag_space + 1;
        }

        // [2] optional prefix
        if (ptr < endp && *ptr == ':')
        {
            ++ptr;
            const char* space_pos = detail::find_first_space_fast(ptr, endp);
            if (space_pos == endp)
            {
                msg.prefix = { ptr, gsl::narrow_cast<std::size_t>(endp - ptr) };
                Ensures(msg.param_count <= IrcMessage::max_params);
                return msg;
            }
            msg.prefix = { ptr, gsl::narrow_cast<std::size_t>(space_pos - ptr) };
            ptr = space_pos + 1;
        }

        // [3] command
        {
            const char* space_pos = detail::find_first_space_fast(ptr, endp);
            if (space_pos != endp)
            {
                msg.command = { ptr, gsl::narrow_cast<std::size_t>(space_pos - ptr) };
                ptr = space_pos + 1;
            }
            else
            {
                msg.command = { ptr, gsl::narrow_cast<std::size_t>(endp - ptr) };
                Ensures(msg.param_count <= IrcMessage::max_params);
                return msg;
            }
        }

        // [4] params and trailing
        detail::parse_params_and_trailing_fast(ptr, endp, msg);
        Ensures(msg.param_count <= IrcMessage::max_params);
        return msg;
    }

} // namespace twitch_bot
