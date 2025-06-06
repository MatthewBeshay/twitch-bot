#pragma once

#include <cstddef>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace twitch_bot {

using TagMap   = std::unordered_map<std::string_view, std::string_view>;
using ParamVec = std::vector<std::string_view>;

namespace detail {

/// Split a semicolon-delimited "key[=value]" list and insert entries into out.
/// @param tag_list  Content between '@' and first space (excluding '@').
/// @param out       Map to populate: each element is key->value, or key->"" if no '='.
inline void parseTags(std::string_view tag_list, TagMap& out) noexcept
{
    std::size_t start = 0;
    const std::size_t len = tag_list.size();

    while (start < len) {
        // Find next semicolon (or end of string)
        std::size_t sep = tag_list.find(';', start);
        if (sep == std::string_view::npos) {
            sep = len;
        }

        // Extract one "key" or "key=value"
        std::string_view kv{ tag_list.data() + start, sep - start };
        if (!kv.empty()) {
            std::size_t eq = kv.find('=');
            if (eq != std::string_view::npos) {
                // key = kv.substr(0, eq), value = kv.substr(eq+1)
                out.emplace(kv.substr(0, eq), kv.substr(eq + 1));
            } else {
                // No '=', so value is empty string_view
                out.emplace(kv, std::string_view{});
            }
        }

        start = sep + 1;
    }
}

} // namespace detail

/// A minimal parsed IRC message. All fields are string_views into the original raw_line.
/// Caller must ensure that raw_line outlives the returned IrcMessage.
struct IrcMessage {
    TagMap           tags;     ///< IRC tags (each key/value is a string_view into raw_line)
    std::string_view prefix;   ///< Optional prefix (e.g. "user!user@host"); empty if none
    std::string_view command;  ///< IRC command (e.g. "PRIVMSG", "PING"); empty if none
    ParamVec         params;   ///< Parameters between command and trailing section
    std::string_view trailing; ///< Trailing part (text after literal " :"); empty if none
};

/// Parse exactly one raw IRC line (excluding the final CRLF) into an IrcMessage.
/// The returned string_views point into raw_line. Caller must keep raw_line valid.
///
/// Format:
///   [ '@' <tags> <space> ] [ ':' <prefix> <space> ] <command> <params> [ ' :' <trailing> ]
///
/// Example raw_line (no "\\r\\n"):
///   "@badge-info=subscriber;color=#1E90FF :user!user@host PRIVMSG #chan :Hello!"
/// @param raw_line  Single IRC line without trailing CRLF.
/// @return          Parsed IrcMessage with tags, prefix, command, params, and trailing.
IrcMessage parseIrcLine(std::string_view raw_line)
{
    IrcMessage msg;
    msg.tags.reserve(8);    // Most lines have few tags
    msg.params.reserve(4);  // Most commands have few parameters

    const char* const data   = raw_line.data();
    const std::size_t length = raw_line.size();
    std::size_t pos = 0;

    // (1) Tags: if first character is '@'
    if (pos < length && data[pos] == '@') {
        std::size_t end_tags = raw_line.find(' ', pos);
        if (end_tags == std::string_view::npos) {
            // Malformed: no space after tags; treat entire remainder as tag list
            detail::parseTags(raw_line.substr(1), msg.tags);
            return msg;
        }

        // Parse content between '@' and first space
        detail::parseTags(
            std::string_view{ data + 1, end_tags - 1 },
            msg.tags
        );
        pos = end_tags + 1;
    }

    // (2) Prefix: if next character is ':'
    if (pos < length && data[pos] == ':') {
        std::size_t end_prefix = raw_line.find(' ', pos);
        if (end_prefix == std::string_view::npos) {
            // Malformed: no space after prefix; treat remainder as prefix
            msg.prefix = std::string_view{ data + pos + 1, length - (pos + 1) };
            return msg;
        }

        // Exclude leading ':'
        msg.prefix = std::string_view{ data + pos + 1, end_prefix - (pos + 1) };
        pos = end_prefix + 1;
    }

    // (3) Command: token until next space or end
    if (pos >= length) {
        return msg; // No command present
    }
    {
        std::size_t end_command = raw_line.find(' ', pos);
        if (end_command == std::string_view::npos) {
            // Entire remainder is command
            msg.command = std::string_view{ data + pos, length - pos };
            return msg;
        }

        msg.command = std::string_view{ data + pos, end_command - pos };
        pos = end_command + 1;
    }

    // (4) Parameters and trailing: loop until end or a ':' starting a trailing section
    while (pos < length) {
        if (data[pos] == ':') {
            // Remainder after ':' is trailing text
            msg.trailing = std::string_view{ data + pos + 1, length - (pos + 1) };
            break;
        }

        // Find next space to isolate one parameter
        std::size_t end_param = raw_line.find(' ', pos);
        if (end_param == std::string_view::npos) {
            // Last parameter (no more spaces)
            msg.params.emplace_back(std::string_view{ data + pos, length - pos });
            break;
        }

        msg.params.emplace_back(std::string_view{ data + pos, end_param - pos });
        pos = end_param + 1;
    }

    return msg;
}

} // namespace twitch_bot
