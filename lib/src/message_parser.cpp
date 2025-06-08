#include "message_parser.hpp"

#include <string_view>

namespace twitch_bot {
namespace {

/// Split "k[=v];k2[=v2]" into out; uses remove_prefix to advance.
/// Expect out was cleared and reserved first.
inline void parseTags(std::string_view tag_list,
                      TagList&         out) noexcept
{
    while (!tag_list.empty()) {
        // find delimiter
        auto sep = tag_list.find(';');
        auto kv  = tag_list.substr(0, sep);

        if (!kv.empty()) {
            auto eq = kv.find('=');
            if (eq != std::string_view::npos) {
                out.emplace_back(kv.substr(0, eq),
                                 kv.substr(eq + 1));
            } else {
                out.emplace_back(kv, std::string_view{});
            }
        }

        if (sep == std::string_view::npos)
            break;  // done
        tag_list.remove_prefix(sep + 1);
    }
}

} // namespace

IrcMessage parseIrcLine(std::string_view raw_line) noexcept
{
    IrcMessage msg;

    // prepare for small number of tags/params, but cap tags at 32
    msg.tags.clear();
    msg.tags.reserve(32);
    msg.params.clear();
    msg.params.reserve(4);

    const char*       data   = raw_line.data();
    size_t            pos    = 0;
    const size_t      length = raw_line.size();

    // [1] parse tags if any
    if (pos < length && data[pos] == '@') {
        auto end = raw_line.find(' ', pos);
        if (end == std::string_view::npos) {
            parseTags(raw_line.substr(1), msg.tags);
            return msg;
        }
        parseTags(raw_line.substr(1, end - 1), msg.tags);
        pos = end + 1;
    }

    // [2] parse prefix if any
    if (pos < length && data[pos] == ':') {
        auto end = raw_line.find(' ', pos);
        if (end == std::string_view::npos) {
            msg.prefix = {data + pos + 1,
                          length - (pos + 1)};
            return msg;
        }
        msg.prefix = {data + pos + 1,
                      end    - (pos + 1)};
        pos = end + 1;
    }

    // [3] parse command
    if (pos >= length)
        return msg;

    {
        auto end = raw_line.find(' ', pos);
        if (end == std::string_view::npos) {
            msg.command = {data + pos,
                           length - pos};
            return msg;
        }
        msg.command = {data + pos,
                       end    - pos};
        pos = end + 1;
    }

    // [4] parse params + trailing
    while (pos < length) {
        if (data[pos] == ':') {
            // rest is trailing
            msg.trailing = {data + pos + 1,
                            length - (pos + 1)};
            break;
        }
        auto end = raw_line.find(' ', pos);
        if (end == std::string_view::npos) {
            msg.params.emplace_back(data + pos,
                                    length - pos);
            break;
        }
        msg.params.emplace_back(data + pos,
                                end   - pos);
        pos = end + 1;
    }

    return msg;
}

} // namespace twitch_bot
