#pragma once

// C++ Standard Library
#include <array>
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
    std::array<std::string_view, max_params> params; ///< middle parameters
    uint8_t param_count : 5 = 0; ///< populated entries in params
    uint8_t is_moderator : 1 = 0; ///< tag "mod=1" present
    uint8_t is_broadcaster : 1 = 0; ///< broadcaster badge present
    std::string_view raw_tags; ///< full tag block (no leading '@')
    std::string_view prefix; ///< server or user prefix (no ':')
    std::string_view trailing; ///< text after the trailing ':'

    static constexpr std::size_t mod_tag_len = 5; ///< length of "mod=1"
    static constexpr std::size_t broadcaster_tag_len = 13; ///< length of "broadcaster/1"
    static constexpr std::size_t badges_prefix_len = 7; ///< length of "badges="

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

    // [1] tags block
    if (ptr < endp && *ptr == '@') {
        ++ptr;
        const char* tag_space = static_cast<const char*>(std::memchr(ptr, ' ', endp - ptr));
        if (!tag_space) {
            msg.raw_tags = {ptr, gsl::narrow_cast<std::size_t>(endp - ptr)};
            Ensures(msg.param_count <= IrcMessage::max_params);
            return msg;
        }

        msg.raw_tags = {ptr, gsl::narrow_cast<std::size_t>(tag_space - ptr)};

        // pick out moderator/broadcaster flags
        const char* field_ptr = ptr;
        while (field_ptr < tag_space) {
            const char* kv_sep
                = static_cast<const char*>(std::memchr(field_ptr, ';', tag_space - field_ptr));
            if (!kv_sep) {
                kv_sep = tag_space;
            }
            const std::size_t kv_len = gsl::narrow_cast<std::size_t>(kv_sep - field_ptr);

            if (!msg.is_moderator && kv_len == IrcMessage::mod_tag_len
                && std::memcmp(field_ptr, "mod=1", IrcMessage::mod_tag_len) == 0) {
                msg.is_moderator = 1;
            }

            if (!msg.is_broadcaster && kv_len == IrcMessage::broadcaster_tag_len
                && std::memcmp(field_ptr, "broadcaster/1", IrcMessage::broadcaster_tag_len) == 0) {
                msg.is_broadcaster = 1;
            }

            if (!msg.is_broadcaster && kv_len > IrcMessage::badges_prefix_len
                && std::memcmp(field_ptr, "badges=", IrcMessage::badges_prefix_len) == 0) {
                std::string_view badges{field_ptr + IrcMessage::badges_prefix_len,
                                        kv_len - IrcMessage::badges_prefix_len};
                if (badges.find("broadcaster/1") != std::string_view::npos) {
                    msg.is_broadcaster = 1;
                }
            }

            if (kv_sep < tag_space) {
                field_ptr = kv_sep + 1;
            } else {
                field_ptr = tag_space;
            }
        }

        ptr = tag_space + 1;
    }

    // [2] prefix
    if (ptr < endp && *ptr == ':') {
        ++ptr;
        const char* space_pos = static_cast<const char*>(std::memchr(ptr, ' ', endp - ptr));
        if (!space_pos) {
            msg.prefix = {ptr, gsl::narrow_cast<std::size_t>(endp - ptr)};
            Ensures(msg.param_count <= IrcMessage::max_params);
            return msg;
        }
        msg.prefix = {ptr, gsl::narrow_cast<std::size_t>(space_pos - ptr)};
        ptr = space_pos + 1;
    }

    // [3] command
    {
        const char* space_pos = static_cast<const char*>(std::memchr(ptr, ' ', endp - ptr));
        if (space_pos) {
            msg.command = {ptr, gsl::narrow_cast<std::size_t>(space_pos - ptr)};
            ptr = space_pos + 1;
        } else {
            msg.command = {ptr, gsl::narrow_cast<std::size_t>(endp - ptr)};
            Ensures(msg.param_count <= IrcMessage::max_params);
            return msg;
        }
    }

    // [4] parameters and trailing
    const char* token_start = ptr;
    while (ptr < endp) {
        char ch = *ptr++;
        if (ch == ' ' && msg.param_count < IrcMessage::max_params) {
            msg.params[msg.param_count++]
                = {token_start, gsl::narrow_cast<std::size_t>(ptr - token_start - 1)};
            token_start = ptr;
        } else if (ch == ':' && token_start == ptr - 1) {
            msg.trailing = {ptr, gsl::narrow_cast<std::size_t>(endp - ptr)};
            Ensures(msg.param_count <= IrcMessage::max_params);
            return msg;
        }
    }

    // final parameter (if any)
    if (token_start < endp && msg.param_count < IrcMessage::max_params) {
        msg.params[msg.param_count++]
            = {token_start, gsl::narrow_cast<std::size_t>(endp - token_start)};
    }

    Ensures(msg.param_count <= IrcMessage::max_params);
    return msg;
}

} // namespace twitch_bot
