#pragma once

#include <string_view>
#include <vector>
#include <utility>

namespace twitch_bot {

    /// A key/value tag from an IRC @-tag list.
    using Tag = std::pair<std::string_view, std::string_view>;

    /// TagList and ParamList using only std::vector, with a small reserve.
    using TagList = std::vector<Tag>;
    using ParamList = std::vector<std::string_view>;

    /**
     * @brief A parsed IRC message.
     *
     * All string_views refer into the original raw_line,
     * so raw_line must outlive the result.
     */
    struct IrcMessage {
        TagList          tags;     //!< key/value pairs from IRC @-tags
        std::string_view prefix;   //!< optional "nick!user@host"
        std::string_view command;  //!< e.g. "PRIVMSG", "PING"
        ParamList        params;   //!< space-separated parameters
        std::string_view trailing; //!< text after the final " :"

        /// Construct and reserve for the common small case.
        IrcMessage() {
            tags.reserve(32);
            params.reserve(4);
        }

        /// Clear contents but keep capacity for reuse.
        void clear() noexcept {
            tags.clear();
            params.clear();
            prefix = {};
            command = {};
            trailing = {};
        }
    };

    /**
     * @brief Parse one IRC line (no CRLF) into an IrcMessage.
     *
     * Single-pass parser that only walks the input once.
     * After the first call, no further allocations are needed
     * as long as tag/param counts stay <= your reserved sizes.
     *
     * IRC format (briefly):
     *   [ '@' <tags> <space> ]  [ ':' <prefix> <space> ]  <command> <params> [ ' :' <trailing> ]
     *
     * Example rawLine (no trailing "\r\n"):
     *   "@badge-info=subscriber/12;color=#1E90FF :someuser!someuser@someuser.tmi.twitch.tv PRIVMSG #channel :Hello!"
     *
     * @param raw_line  full line from server; references must remain valid
     * @param msg       the IrcMessage to populate (will be cleared)
     * @return reference to `msg` for chaining or reuse
     */
    IrcMessage& parseIrcLine(std::string_view raw_line,
        IrcMessage& msg) noexcept;

} // namespace twitch_bot
