#include "message_parser.hpp"

#include <cstddef>

namespace twitch_bot {

    IrcMessage& parseIrcLine(std::string_view raw_line,
        IrcMessage& msg) noexcept
    {
    // reset for this parse
    msg.clear();

    const char* begin = raw_line.data();
    const char* end   = begin + raw_line.size();
    const char* p     = begin;

    // [1] @-tags
    if (p < end && *p == '@') {
        ++p; // skip '@'
        while (p < end && *p != ' ') {
            // parse key
            const char* key_begin = p;
            while (p < end && *p != '=' && *p != ';' && *p != ' ')
                ++p;
            std::string_view key{ key_begin,
                static_cast<size_t>(p - key_begin) };

            // parse optional value
            std::string_view val{};
            if (p < end && *p == '=') {
                ++p; // skip '='
                const char* val_begin = p;
                while (p < end && *p != ';' && *p != ' ')
                    ++p;
                val = { val_begin,
                    static_cast<size_t>(p - val_begin) };
            }

            msg.tags.emplace_back(key, val);

            if (p < end && *p == ';')
                ++p; // skip delimiter
        }
        if (p < end && *p == ' ')
            ++p; // skip space after tags
    }

    // [2] prefix
    if (p < end && *p == ':') {
        ++p; // skip ':'
        const char* pre_begin = p;
        while (p < end && *p != ' ')
            ++p;
        msg.prefix = { pre_begin,
            static_cast<size_t>(p - pre_begin) };
        if (p < end)
            ++p; // skip space
    }

    // [3] command
    if (p < end) {
        const char* cmd_begin = p;
        while (p < end && *p != ' ')
            ++p;
        msg.command = { cmd_begin,
            static_cast<size_t>(p - cmd_begin) };
        if (p < end)
            ++p; // skip space
    }

    // [4] parameters and trailing
    const char* token_begin = p;
    while (p < end) {
        if (*p == ' ') {
            // end of one parameter
            msg.params.emplace_back(token_begin,
                static_cast<size_t>(p - token_begin));
            ++p; // skip space
            token_begin = p;
        }
        else if (*p == ':' && token_begin == p) {
            // trailing part: rest of line
            ++p; // skip ':'
            msg.trailing = { p,
                static_cast<size_t>(end - p) };
            break;
        }
        else {
            ++p;
        }
    }
    // leftover parameter if no trailing text
    if (token_begin < end && msg.trailing.empty()) {
        msg.params.emplace_back(token_begin,
            static_cast<size_t>(end - token_begin));
    }

    return msg;
}

} // namespace twitch_bot
