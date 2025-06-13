#pragma once

#include "utils/attributes.hpp"

#include <cassert>
#include <cstddef>
#include <cstring>
#include <string_view>

namespace twitch_bot {

// Parsed IRC message with zero allocations.
// Views into the original buffer; no ownership here.
struct IrcMessage {
    static constexpr std::size_t MAX_PARAMS = 16;

    std::string_view prefix;               // server or user prefix
    std::string_view command;              // IRC command (e.g. PRIVMSG)
    std::string_view params[MAX_PARAMS];   // up to MAX_PARAMS middle parameters
    std::size_t      param_count = 0;      // number of valid entries in params
    std::string_view trailing;             // text after the ':' delimiter

    bool is_moderator   = false;           // true if tag "mod=1" was seen
    bool is_broadcaster = false;           // true if tag "broadcaster/1" was seen
};

// Parse one raw IRC line into an IrcMessage.
// Preconditions: raw.empty() || raw.data() != nullptr
// Postcondition : result.param_count <= MAX_PARAMS
TB_FORCE_INLINE
IrcMessage parse_irc_line(std::string_view raw) noexcept
{
    // precondition check
    assert(raw.empty() || raw.data() != nullptr);

    IrcMessage msg;
    const char* p   = raw.data();
    const char* end = p + raw.size();

    // [1] Extract tags block if it begins with '@'
    if (p < end && *p == '@') {
        ++p;
        auto const tag_end = static_cast<const char*>(memchr(p, ' ', end - p));
        if (!tag_end) {
            return msg;  // malformed: no space after tags
        }

        // scan once for both 'mod=1' and 'broadcaster/1'
        bool found_mod   = false;
        bool found_bcast = false;
        for (auto q = p; q < tag_end && (!found_mod || !found_bcast); ++q) {
            if (!found_mod &&
                q + 4 < tag_end &&
                q[0]=='m' && q[1]=='o' && q[2]=='d' &&
                q[3]=='=' && q[4]=='1')
            {
                found_mod = true;
            }
            else if (!found_bcast &&
                     q + 12 < tag_end &&
                     std::memcmp(q, "broadcaster/1", 13) == 0)
            {
                found_bcast = true;
            }
        }
        msg.is_moderator   = found_mod;
        msg.is_broadcaster = found_bcast;

        p = tag_end + 1;  // skip the space
    }

    // [2] Extract prefix if present (starts with ':')
    if (p < end && *p == ':') {
        ++p;
        if (auto const sp = static_cast<const char*>(memchr(p, ' ', end - p))) {
            msg.prefix = { p, static_cast<size_t>(sp - p) };
            p = sp + 1;
        }
        else {
            return msg;  // malformed: no space after prefix
        }
    }

    // [3] Extract command (up to next space or end)
    if (auto const sp = static_cast<const char*>(memchr(p, ' ', end - p))) {
        msg.command = { p, static_cast<size_t>(sp - p) };
        p = sp + 1;
    }
    else {
        msg.command = { p, static_cast<size_t>(end - p) };
        return msg;  // no parameters or trailing section
    }

    // [4] Parse middle parameters and trailing text in one pass
    auto tok = p;
    while (p < end) {
        char c = *p++;
        if (c == ' ' && msg.param_count < IrcMessage::MAX_PARAMS) {
            // finish one parameter
            msg.params[msg.param_count++] =
                { tok, static_cast<size_t>(p - tok - 1) };
            tok = p;
        }
        else if (c == ':' && tok == p - 1) {
            // rest is trailing text
            msg.trailing = { p, static_cast<size_t>(end - p) };
            // postcondition check
            assert(msg.param_count <= IrcMessage::MAX_PARAMS);
            return msg;
        }
    }

    // capture final parameter if any remain
    if (tok < end && msg.param_count < IrcMessage::MAX_PARAMS) {
        msg.params[msg.param_count++] =
            { tok, static_cast<size_t>(end - tok) };
    }

    // postcondition check
    assert(msg.param_count <= IrcMessage::MAX_PARAMS);
    return msg;
}

}  // namespace twitch_bot
