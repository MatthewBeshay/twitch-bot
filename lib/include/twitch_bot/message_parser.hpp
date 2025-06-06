#pragma once

#include <string_view>
#include <unordered_map>
#include <vector>

namespace twitch_bot {

using TagMap   = std::unordered_map<std::string_view, std::string_view>;
using ParamVec = std::vector<std::string_view>;

/// A minimal parsed IRC message. All fields are string_views into the original rawLine.
/// The caller must guarantee that rawLine outlives the returned IrcMessage.
struct IrcMessage {
    TagMap           tags;     ///< IRC tags (each key/value is a string_view into rawLine)
    std::string_view prefix;   ///< Optional prefix (for example, "foo!bar@baz"); empty if none
    std::string_view command;  ///< IRC command (for example, "PRIVMSG", "PING"); empty if none
    ParamVec         params;   ///< All parameters between command and trailing section
    std::string_view trailing; ///< Trailing part (anything after " :"); empty if none
};

/// Parse exactly one raw IRC line (without the final CRLF) into an IrcMessage.
/// The returned string_views all point into rawLine. It is the caller's responsibility
/// to ensure rawLine outlives the returned IrcMessage (for example, process it immediately
/// before reusing the buffer).
///
/// IRC format (briefly):
///   [ '@' <tags> <space> ]  [ ':' <prefix> <space> ]  <command> <params> [ ' :' <trailing> ]
///
/// Example rawLine (no trailing "\r\n"):
///   "@badge-info=subscriber/12;color=#1E90FF :someuser!someuser@someuser.tmi.twitch.tv PRIVMSG #channel :Hello!"
///
/// @param rawLine  A single IRC line (without trailing CRLF). Must remain valid while using the result.
/// @return         Parsed IrcMessage with tags, prefix, command, params, and trailing.
IrcMessage parseIrcLine(std::string_view rawLine);

} // namespace twitch_bot
