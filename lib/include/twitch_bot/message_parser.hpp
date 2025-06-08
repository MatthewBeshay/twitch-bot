#pragma once

#include <string_view>
#include <vector>
#include <utility>

namespace twitch_bot {

using TagList  = std::vector<std::pair<std::string_view, std::string_view>>;
using ParamVec = std::vector<std::string_view>;

/**
 * @brief A parsed IRC message.
 *
 * All string_views in this struct refer into the
 * original raw_line, so raw_line must outlive the result.
 */
struct IrcMessage {
    TagList          tags;     //!< key/value pairs from IRC @-tags
    std::string_view prefix;   //!< optional "nick!user@host"
    std::string_view command;  //!< e.g. "PRIVMSG", "PING"
    ParamVec         params;   //!< space-separated parameters
    std::string_view trailing; //!< text after the final " :"
};

/**
 * @brief Parse one IRC line (no CRLF) into an IrcMessage.
 *
 * @param raw_line  full line from server; references must remain valid
 * @return a fully populated IrcMessage; empty fields if absent
 * @note  Containers in IrcMessage reserve for small sizes to minimize
 *        dynamic allocations on hot paths.
 */
IrcMessage parseIrcLine(std::string_view raw_line) noexcept;

} // namespace twitch_bot
