#pragma once

// C++ Standard Library
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Boost.Asio
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

// Project
#include <tb/parser/irc_message_parser.hpp>
#include <tb/utils/attributes.hpp>
#include <tb/utils/transparent_string_hash.hpp>

namespace twitch_bot {

// Callback for plain chat lines.
using chat_listener_t
    = std::function<void(std::string_view channel, std::string_view user, std::string_view text)>;

// Callback for an IRC command (runs on an executor).
using command_handler_t = std::function<boost::asio::awaitable<void>(IrcMessage msg)>;

// Routes IRC messages to command handlers or chat listeners.
class CommandDispatcher
{
public:
    // Create a dispatcher that spawns work on \p executor.
    explicit CommandDispatcher(boost::asio::any_io_executor executor);

    // Add a handler for \p command (case-sensitive).
    void register_command(std::string_view command, command_handler_t handler);

    // Add a fallback listener for unhandled chat lines.
    void register_chat_listener(chat_listener_t listener);

    // Dispatch a plain chat line without IRC types.
    // Expects channel with no leading '#' and user as the login name.
    // Safe to call from any thread; handlers run on the stored executor.
    void dispatch_text(std::string_view channel, std::string_view user, std::string_view text);

    // Dispatch \p msg asynchronously.
    void dispatch(IrcMessage msg);

private:
    // Strip a leading '#'.
    static TB_FORCE_INLINE std::string_view normalize_channel(std::string_view raw) noexcept
    {
        return (!raw.empty() && raw.front() == '#') ? raw.substr(1) : raw;
    }

    // Part before '!' in \p prefix.
    static TB_FORCE_INLINE std::string_view extract_user(std::string_view prefix) noexcept
    {
        auto pos = prefix.find('!');
        return (pos != std::string_view::npos) ? prefix.substr(0, pos) : prefix;
    }

    // Split "!cmd args" into \p out_cmd and \p out_args.
    static TB_FORCE_INLINE void split_command(std::string_view text,
                                              std::string_view& out_cmd,
                                              std::string_view& out_args) noexcept
    {
        if (text.size() > 1 && text.front() == '!') {
            auto pos = text.find(' ');
            if (pos == std::string_view::npos) {
                out_cmd = text.substr(1);
                out_args = {};
            } else {
                out_cmd = text.substr(1, pos - 1);
                out_args = text.substr(pos + 1);
            }
        } else {
            out_cmd = {};
            out_args = {};
        }
    }

    boost::asio::any_io_executor executor_;
    std::unordered_map<std::string,
                       command_handler_t,
                       TransparentBasicStringHash<char>,
                       TransparentBasicStringEq<char>>
        commands_;
    std::vector<chat_listener_t> chat_listeners_;

    // Route helper that can optionally include tags and role flags from the source message.
    // Channel must be normalized (no '#'). User must be the login part.
    void route_text(std::string_view channel,
                    std::string_view user,
                    std::string_view text,
                    std::string_view raw_tags = {},
                    bool is_moderator = false,
                    bool is_broadcaster = false);
};

} // namespace twitch_bot
