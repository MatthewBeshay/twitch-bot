#pragma once

// C++ Standard Library
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

// 3rd-party
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

#include <gsl/gsl>

// Project
#include "utils/attributes.hpp"

namespace twitch_bot {

// Parsed IRC message with zero allocations.
// Views into the original buffer; this struct does not own any data.
struct IrcMessage {
    // Maximum number of parameters between command and trailing.
    static constexpr std::size_t max_params = 16;

    // IRC command, e.g. "PRIVMSG".
    std::string_view command;

    // Middle parameters of the command.
    std::string_view params[max_params];

    // Number of entries populated in params.
    uint8_t param_count : 5;

    // True if tag "mod=1" was present.
    uint8_t is_moderator : 1;

    // True if the broadcaster badge was present.
    uint8_t is_broadcaster : 1;

    // Raw tag block, e.g. "mod=1;badges=…".
    std::string_view raw_tags;

    // Server or user prefix (without leading ':').
    std::string_view prefix;

    // Trailing text after ':'.
    std::string_view trailing;

    // mutable span (only in non-const context)
    TB_FORCE_INLINE
    gsl::span<std::string_view> parameters() noexcept
    {
        return {params, params + param_count};
    }

    // read-only span (for const objects)
    TB_FORCE_INLINE
    gsl::span<const std::string_view> parameters() const noexcept
    {
        return {params, params + param_count};
    }

    // Return the first value for key, or empty if not present.
    // O(n) in the length of raw_tags.
    //
    // Precondition: key is not empty.
    TB_FORCE_INLINE
    std::string_view get_tag(std::string_view key) const noexcept
    {
        Expects(!key.empty());

        const char *p = raw_tags.data();
        const char *const end = p + raw_tags.size();
        const std::size_t klen = key.size();

        while (p < end) {
            // match "key="
            if (gsl::narrow_cast<std::size_t>(end - p) >= klen + 1
                && std::memcmp(p, key.data(), klen) == 0 && p[klen] == '=') {
                const char *vs = p + klen + 1;
                const char *ve = static_cast<const char *>(std::memchr(vs, ';', end - vs));
                if (!ve) {
                    ve = end;
                }
                return {vs, gsl::narrow_cast<std::size_t>(ve - vs)};
            }
            // skip to next semicolon
            const char *sep = static_cast<const char *>(std::memchr(p, ';', end - p));
            p = sep ? sep + 1 : end;
        }

        return {};
    }
};

// Parse one raw IRC line (no CRLF) into an IrcMessage.
// All string_views refer to the original buffer; no heap allocations.
//
// Precondition: raw.empty() || raw.data() != nullptr.
// Postcondition: result.param_count <= max_params.
TB_FORCE_INLINE
IrcMessage parse_irc_line(std::string_view raw) noexcept
{
    Expects(raw.empty() || raw.data() != nullptr);

    IrcMessage msg{};
    const char *TB_RESTRICT p = raw.data();
    const char *const end = p + raw.size();

    // [1] tags block?
    if (p < end && *p == '@') {
        ++p;
        const char *tag_end = static_cast<const char *>(std::memchr(p, ' ', end - p));
        if (!tag_end) {
            msg.raw_tags = {p, gsl::narrow_cast<std::size_t>(end - p)};
            Ensures(msg.param_count <= IrcMessage::max_params);
            return msg;
        }
        msg.raw_tags = {p, gsl::narrow_cast<std::size_t>(tag_end - p)};

        // scan tags for moderator, broadcaster and badges
        const char *q = p;
        while (q < tag_end) {
            // locate end of current tag
            const char *kv_end = static_cast<const char *>(std::memchr(q, ';', tag_end - q));
            if (!kv_end) {
                kv_end = tag_end;
            }
            const std::size_t kv_len = gsl::narrow_cast<std::size_t>(kv_end - q);

            // mark moderator
            if (!msg.is_moderator && kv_len == 5 && std::memcmp(q, "mod=1", 5) == 0) {
                msg.is_moderator = 1;
            }

            // mark broadcaster if standalone
            if (!msg.is_broadcaster && kv_len == 13 && std::memcmp(q, "broadcaster/1", 13) == 0) {
                msg.is_broadcaster = 1;
            }

            // parse badges list for broadcaster badge
            if (!msg.is_broadcaster && kv_len > 7 && std::memcmp(q, "badges=", 7) == 0) {
                std::string_view badges{q + 7, kv_len - 7};
                if (badges.find("broadcaster/1") != std::string_view::npos) {
                    msg.is_broadcaster = 1;
                }
            }

            // advance to next tag
            q = (kv_end < tag_end ? kv_end + 1 : tag_end);
        }
        p = tag_end + 1;
    }

    // [2] prefix?
    if (p < end && *p == ':') {
        ++p;
        const char *sp = static_cast<const char *>(std::memchr(p, ' ', end - p));
        if (!sp) {
            msg.prefix = {p, gsl::narrow_cast<std::size_t>(end - p)};
            Ensures(msg.param_count <= IrcMessage::max_params);
            return msg;
        }
        msg.prefix = {p, gsl::narrow_cast<std::size_t>(sp - p)};
        p = sp + 1;
    }

    // [3] command
    {
        const char *sp = static_cast<const char *>(std::memchr(p, ' ', end - p));
        if (sp) {
            msg.command = {p, gsl::narrow_cast<std::size_t>(sp - p)};
            p = sp + 1;
        } else {
            msg.command = {p, gsl::narrow_cast<std::size_t>(end - p)};
            Ensures(msg.param_count <= IrcMessage::max_params);
            return msg;
        }
    }

    // [4] parameters and trailing
    const char *token_start = p;
    while (p < end) {
        char c = *p++;
        if (c == ' ' && msg.param_count < IrcMessage::max_params) {
            msg.params[msg.param_count++]
                = {token_start, gsl::narrow_cast<std::size_t>(p - token_start - 1)};
            token_start = p;
        } else if (c == ':' && token_start == p - 1) {
            msg.trailing = {p, gsl::narrow_cast<std::size_t>(end - p)};
            Ensures(msg.param_count <= IrcMessage::max_params);
            return msg;
        }
    }

    // final parameter
    if (token_start < end && msg.param_count < IrcMessage::max_params) {
        msg.params[msg.param_count++]
            = {token_start, gsl::narrow_cast<std::size_t>(end - token_start)};
    }

    Ensures(msg.param_count <= IrcMessage::max_params);
    return msg;
}

} // namespace twitch_bot
