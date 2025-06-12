#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace twitch_bot {

/// @brief Parsed IRC message: zero-allocation, fixed-size storage.
/// @details We only track whether the user is a moderator, so we don't
///          need to keep every single tag around.
struct IrcMessage {
    static constexpr std::size_t MaxParams = 16;  ///< max middle params

    //-- prefix, command, parameters, trailing -----------------------------
    const char*   prefix      = nullptr;  ///< e.g. "nick!user@host"
    uint16_t      prefixLen   = 0;

    const char*   command     = nullptr;  ///< e.g. "PRIVMSG"
    uint16_t      commandLen  = 0;

    const char*   params[MaxParams];      ///< each middle param
    uint16_t      paramLens[MaxParams];
    std::size_t   paramCount = 0;

    const char*   trailing    = nullptr;  ///< text after " :"
    uint32_t      trailingLen = 0;

    bool          isModerator = false;    ///< set if @mod=1 was present

    /// Reset all fields to "empty" for reuse.
    void clear() noexcept {
        prefix      = nullptr; prefixLen   = 0;
        command     = nullptr; commandLen  = 0;
        paramCount  = 0;
        trailing    = nullptr; trailingLen = 0;
        isModerator = false;
    }
};

/// @brief Parse one IRC line (no CRLF) into an `IrcMessage`.
/// 
/// IRC format (briefly):
///   ['@' < tags > <space>][':' < prefix > <space>]  <command> <params>[' :' < trailing > ]
///
/// Example rawLine(no trailing "\r\n") :
///   "@badge-info=subscriber/12;color=#1E90FF :someuser!someuser@someuser.tmi.twitch.tv PRIVMSG #channel :Hello!"
/// 
/// @param raw Pointer to the raw line buffer.
/// @param len Length of the buffer.
/// @param msg Message struct to populate (must outlive raw).
/// @return Reference to `msg` (for chaining).
inline IrcMessage& parseIrcLine(const char* raw,
                                 std::size_t len,
                                 IrcMessage& msg) noexcept
{
    msg.clear();
    const char* p   = raw;
    const char* end = raw + len;

    // [1] @-tags -> we only care about "mod"
    if (p < end && *p == '@') {
        ++p; // skip '@'
        while (p < end && *p != ' ') {
            // find end of key
            const char* k0 = p;
            while (p < end && *p != '=' && *p != ';' && *p != ' ')
                ++p;
            std::size_t klen = p - k0;

            // if it's "mod", grab the value
            bool checkMod = (klen == 3
                             && k0[0]=='m' && k0[1]=='o' && k0[2]=='d');

            // parse optional value
            if (checkMod && p < end && *p == '=') {
                ++p; // skip '='
                if (p < end && *p == '1')
                    msg.isModerator = true;
                // skip until next ';' or ' '
                while (p < end && *p != ';' && *p != ' ')
                    ++p;
            } else {
                // skip over "=..." or until ';'/' '
                if (p < end && *p == '=') {
                    ++p;
                    while (p < end && *p != ';' && *p != ' ')
                        ++p;
                }
            }

            // done with this tag
            if (p < end && *p == ';') ++p;
        }
        if (p < end && *p == ' ') ++p;
    }

    // [2] prefix
    if (p<end && *p==':') {
        ++p;
        const char* x0 = p;
        while (p<end && *p!=' ') ++p;
        msg.prefix    = x0;
        msg.prefixLen = static_cast<uint16_t>(p - x0);
        if (p<end) ++p;
    }

    // [3] command
    if (p<end) {
        const char* x0 = p;
        while (p<end && *p!=' ') ++p;
        msg.command    = x0;
        msg.commandLen = static_cast<uint16_t>(p - x0);
        if (p<end) ++p;
    }

    // [4] params + trailing
    const char* tok = p;
    while (p<end) {
        if (*p==' ') {
            auto plen = static_cast<uint16_t>(p - tok);
            if (msg.paramCount < IrcMessage::MaxParams) {
                auto i = msg.paramCount++;
                msg.params[i]    = tok;
                msg.paramLens[i] = plen;
            }
            ++p; tok = p;
        }
        else if (*p==':' && tok==p) {
            ++p;
            msg.trailing    = p;
            msg.trailingLen = static_cast<uint32_t>(end - p);
            break;
        }
        else {
            ++p;
        }
    }
    if (tok<end && msg.trailing==nullptr) {
        auto plen = static_cast<uint16_t>(end - tok);
        if (msg.paramCount < IrcMessage::MaxParams) {
            auto i = msg.paramCount++;
            msg.params[i]    = tok;
            msg.paramLens[i] = plen;
        }
    }

    return msg;
}

} // namespace twitch_bot