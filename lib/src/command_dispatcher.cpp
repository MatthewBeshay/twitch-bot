#include "command_dispatcher.hpp"

#include <iostream>

#include <boost/asio/use_awaitable.hpp>

namespace twitch_bot {

boost::asio::awaitable<void> CommandDispatcher::dispatch(
    IrcMessage const& msg)
{
    // Only proceed if this is a PRIVMSG with at least one parameter.
    if (msg.command != "PRIVMSG" || msg.params.empty()) {
        co_return;
    }

    // Extract channel name (strip leading '#' if present).
    std::string_view raw_chan = msg.params[0];
    std::string_view channel = (!raw_chan.empty() && raw_chan.front() == '#')
        ? raw_chan.substr(1)
        : raw_chan;

    // Extract user from prefix (format: ":username!username@host").
    std::string_view user;
    if (!msg.prefix.empty()) {
        std::string_view prefix = msg.prefix;
        if (prefix.front() == ':') {
            prefix.remove_prefix(1);
        }

        const auto excl_pos = prefix.find('!');
        user = (excl_pos != std::string_view::npos)
            ? prefix.substr(0, excl_pos)
            : prefix;
    }

    // Check if the message starts with '!' (command).
    std::string_view text = msg.trailing;
    if (!text.empty() && text.front() == '!') {
        auto space_pos = text.find(' ');
        std::string_view cmd = (space_pos == std::string_view::npos)
            ? text
            : text.substr(0, space_pos);
        std::string_view args = (space_pos == std::string_view::npos)
            ? std::string_view{}
            : text.substr(space_pos + 1);

        // Heterogeneous lookup: find handler without string allocation.
        auto it = commandMap_.find(cmd);
        if (it != commandMap_.end()) {
            try {
                co_await it->second(channel, user, args, msg.tags);
            } catch (const std::exception& e) {
                std::cerr << "[CommandDispatcher] Handler for '"
                          << cmd << "' threw exception: " << e.what() << "\n";
            }
            co_return;
        }
        // If no matching command, fall through to chat listeners.
    }

    // Invoke all registered chat listeners.
    for (auto& callback : chatListeners_) {
        callback(channel, user, text);
    }

    co_return;
}

} // namespace twitch_bot
