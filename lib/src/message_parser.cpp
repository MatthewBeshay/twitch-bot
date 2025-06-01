#include "message_parser.hpp"

#include <cstddef>

namespace twitch_bot {

namespace detail {
    /**
     * @brief  Split a semicolon-delimited "key[=value]" list and insert into out.
     *
     * @param tagList  string_view of everything between the '@' and the first space (no '@').
     * @param out      TagMap to populate (each element is key=>value, or key=>"" if no '=').
     */
    inline void parseTags(std::string_view tagList, TagMap& out) noexcept
    {
        std::size_t start = 0;
        const std::size_t len = tagList.size();

        while (start < len) {
            // Find next semicolon (or end)
            std::size_t sep = tagList.find(';', start);
            if (sep == std::string_view::npos) {
                sep = len;
            }

            // Extract a single "key" or "key=value"
            std::string_view kv{ tagList.data() + start, sep - start };
            if (!kv.empty()) {
                std::size_t eq = kv.find('=');
                if (eq != std::string_view::npos) {
                    // key = kv.substr(0, eq), value = kv.substr(eq+1)
                    out.emplace(kv.substr(0, eq), kv.substr(eq + 1));
                } else {
                    // no '=', so value is empty_view
                    out.emplace(kv, std::string_view{});
                }
            }
            start = sep + 1;
        }
    }
} // namespace detail

IrcMessage parseIrcLine(std::string_view rawLine)
{
    IrcMessage msg;
    msg.tags.reserve(8);    // heuristic: most lines have only a handful of tags
    msg.params.reserve(4);  // heuristic: most commands have a few params

    const char* const data   = rawLine.data();
    const std::size_t length = rawLine.size();
    std::size_t pos = 0;

    // (1) Tags: if the very first character is '@'
    if (pos < length && data[pos] == '@') {
        const std::size_t endTags = rawLine.find(' ', pos);
        if (endTags == std::string_view::npos) {
            // Malformed: no space after tags; treat the entire remainder as tagList
            detail::parseTags(rawLine.substr(1), msg.tags);
            return msg; 
        }

        // Parse everything between '@' and the first space
        detail::parseTags(std::string_view{ data + 1, endTags - 1 }, msg.tags);
        pos = endTags + 1; 
    }

    // (2) Prefix: if the next character is ':'
    if (pos < length && data[pos] == ':') {
        const std::size_t endPrefix = rawLine.find(' ', pos);
        if (endPrefix == std::string_view::npos) {
            // Malformed: no space after prefix; treat remainder as prefix
            msg.prefix = std::string_view{ data + pos + 1, length - (pos + 1) };
            return msg;
        }
        // Exclude the leading ':'
        msg.prefix = std::string_view{ data + pos + 1, endPrefix - (pos + 1) };
        pos = endPrefix + 1;
    }

    // (3) Command: single token until next space (or end)
    if (pos >= length) {
        return msg; // no command at all
    }
    {
        const std::size_t endCommand = rawLine.find(' ', pos);
        if (endCommand == std::string_view::npos) {
            // Entire remainder is the command
            msg.command = std::string_view{ data + pos, length - pos };
            return msg;
        }
        msg.command = std::string_view{ data + pos, endCommand - pos };
        pos = endCommand + 1;
    }

    // (4) Parameters & trailing: loop until end or a ':' at start of a token
    while (pos < length) {
        if (data[pos] == ':') {
            // The remainder (after this ':') is the trailing content
            msg.trailing = std::string_view{ data + pos + 1, length - (pos + 1) };
            break;
        }

        // Find next space to isolate one param
        const std::size_t endParam = rawLine.find(' ', pos);
        if (endParam == std::string_view::npos) {
            // Last parameter (no more spaces)
            msg.params.emplace_back(std::string_view{ data + pos, length - pos });
            break;
        }

        msg.params.emplace_back(std::string_view{ data + pos, endParam - pos });
        pos = endParam + 1;
    }

    return msg;
}

} // namespace twitch_bot
