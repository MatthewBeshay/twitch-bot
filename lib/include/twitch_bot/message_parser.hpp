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

/// Parsed IRC message - views into the original buffer, no ownership or allocations.
struct IrcMessage {
    static constexpr std::size_t max_params = 16; ///< hard limit on middle parameters

    std::string_view command; ///< IRC command (e.g. "PRIVMSG")
    std::string_view params[max_params]; ///< middle parameters
    uint8_t param_count : 5 = 0; ///< populated entries in params
    uint8_t is_moderator : 1 = 0; ///< tag "mod=1" present
    uint8_t is_broadcaster : 1 = 0; ///< broadcaster badge present
    std::string_view raw_tags; ///< full tag block (no leading '@')
    std::string_view prefix; ///< server or user prefix (no ':')
    std::string_view trailing; ///< text after the trailing ':'

    /// Span of parameters (mutable when object is non-const).
    TB_FORCE_INLINE
    gsl::span<std::string_view> parameters() noexcept
    {
        return {params, params + param_count};
    }

    /// Read-only span of parameters.
    TB_FORCE_INLINE
    gsl::span<const std::string_view> parameters() const noexcept
    {
        return {params, params + param_count};
    }

    /// First tag value matching \p key, or empty if absent.
    /// Precondition: \p key is non-empty.
    TB_FORCE_INLINE
    std::string_view get_tag(std::string_view key) const noexcept
    {
        Expects(!key.empty());

        const char* p = raw_tags.data();
        const char* const end = p + raw_tags.size();
        const std::size_t klen = key.size();

        while (p < end) {
            // match "key="
            if (gsl::narrow_cast<std::size_t>(end - p) >= klen + 1
                && std::memcmp(p, key.data(), klen) == 0 && p[klen] == '=') {
                const char* vs = p + klen + 1;
                const char* ve = static_cast<const char*>(std::memchr(vs, ';', end - vs));
                if (!ve)
                    ve = end;
                return {vs, gsl::narrow_cast<std::size_t>(ve - vs)};
            }
            // advance to next tag
            const char* sep = static_cast<const char*>(std::memchr(p, ';', end - p));
            p = sep ? sep + 1 : end;
        }
        return {};
    }
};

/// Parse one raw IRC line (no CRLF) into an IrcMessage.
/// All views refer to \p raw; no allocations.
/// Pre: raw.empty() || raw.data() != nullptr.
/// Post: result.param_count <= max_params.
TB_FORCE_INLINE
IrcMessage parse_irc_line(std::string_view raw) noexcept
{
    Expects(raw.empty() || raw.data() != nullptr);

    IrcMessage msg{};
    const char* TB_RESTRICT p = raw.data();
    const char* const end = p + raw.size();

    // [1] tags block
    if (p < end && *p == '@') {
        ++p;
        const char* tag_end = static_cast<const char*>(std::memchr(p, ' ', end - p));
        if (!tag_end) { // tags only
            msg.raw_tags = {p, gsl::narrow_cast<std::size_t>(end - p)};
            Ensures(msg.param_count <= IrcMessage::max_params);
            return msg;
        }
        msg.raw_tags = {p, gsl::narrow_cast<std::size_t>(tag_end - p)};

        // pick out moderator/broadcaster flags
        const char* q = p;
        while (q < tag_end) {
            const char* kv_end = static_cast<const char*>(std::memchr(q, ';', tag_end - q));
            if (!kv_end)
                kv_end = tag_end;
            const std::size_t kv_len = gsl::narrow_cast<std::size_t>(kv_end - q);

            if (!msg.is_moderator && kv_len == 5 && std::memcmp(q, "mod=1", 5) == 0)
                msg.is_moderator = 1;

            if (!msg.is_broadcaster && kv_len == 13 && std::memcmp(q, "broadcaster/1", 13) == 0)
                msg.is_broadcaster = 1;

            if (!msg.is_broadcaster && kv_len > 7 && std::memcmp(q, "badges=", 7) == 0) {
                std::string_view badges{q + 7, kv_len - 7};
                if (badges.find("broadcaster/1") != std::string_view::npos)
                    msg.is_broadcaster = 1;
            }
            q = kv_end < tag_end ? kv_end + 1 : tag_end;
        }
        p = tag_end + 1;
    }

    // [2] prefix
    if (p < end && *p == ':') {
        ++p;
        const char* sp = static_cast<const char*>(std::memchr(p, ' ', end - p));
        if (!sp) { // prefix only
            msg.prefix = {p, gsl::narrow_cast<std::size_t>(end - p)};
            Ensures(msg.param_count <= IrcMessage::max_params);
            return msg;
        }
        msg.prefix = {p, gsl::narrow_cast<std::size_t>(sp - p)};
        p = sp + 1;
    }

    // [3] command
    {
        const char* sp = static_cast<const char*>(std::memchr(p, ' ', end - p));
        if (sp) {
            msg.command = {p, gsl::narrow_cast<std::size_t>(sp - p)};
            p = sp + 1;
        } else { // command only
            msg.command = {p, gsl::narrow_cast<std::size_t>(end - p)};
            Ensures(msg.param_count <= IrcMessage::max_params);
            return msg;
        }
    }

    // [4] parameters and trailing
    const char* token_start = p;
    while (p < end) {
        char c = *p++;
        if (c == ' ' && msg.param_count < IrcMessage::max_params) {
            msg.params[msg.param_count++]
                = {token_start, gsl::narrow_cast<std::size_t>(p - token_start - 1)};
            token_start = p;
        } else if (c == ':' && token_start == p - 1) { // start of trailing
            msg.trailing = {p, gsl::narrow_cast<std::size_t>(end - p)};
            Ensures(msg.param_count <= IrcMessage::max_params);
            return msg;
        }
    }

    // final parameter (if any)
    if (token_start < end && msg.param_count < IrcMessage::max_params)
        msg.params[msg.param_count++]
            = {token_start, gsl::narrow_cast<std::size_t>(end - token_start)};

    Ensures(msg.param_count <= IrcMessage::max_params);
    return msg;
}

} // namespace twitch_bot
