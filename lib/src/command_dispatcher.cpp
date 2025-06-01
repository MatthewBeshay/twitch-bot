#include "command_dispatcher.hpp"

#include <iostream>

#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace twitch_bot {

boost::asio::awaitable<void> CommandDispatcher::dispatch(
    IrcMessage const& msg)
{
    // 1) Only proceed if this is a PRIVMSG with at least one param (the channel).
    if (msg.command != "PRIVMSG" || msg.params.empty()) {
        co_return;
    }

    // 2) Extract channel name (strip leading '#', if present).
    std::string_view rawChan = msg.params[0];
    std::string_view channel = (rawChan.size() > 0 && rawChan.front() == '#')
        ? rawChan.substr(1)
        : rawChan;

    // 3) Extract user from prefix.
    //    parseIrcLine(...) already removed the leading ':' from msg.prefix,
    //    so msg.prefix == "username!username@...".  We simply split at the '!' now.
    std::string_view user;
    {
      auto excl = msg.prefix.find('!');
      if (excl != std::string_view::npos) {
        user = msg.prefix.substr(0, excl);
      } else {
        user = msg.prefix; // no '!' found; treat entire prefix as user
      }
    }

    // 4) Check if trailing text begins with '!' (indicating a command).
    std::string_view text = msg.trailing;
    if (!text.empty() && text.front() == '!') {
        // Split into command token and the rest as args.
        auto space = text.find(' ');
        std::string_view cmd = (space == std::string_view::npos)
            ? text
            : text.substr(0, space);
        std::string_view args = (space == std::string_view::npos)
            ? std::string_view{}
            : text.substr(space + 1);

        // 5) Heterogeneous lookup: no temporary std::string construction.
        auto it = commandMap_.find(cmd);
        if (it != commandMap_.end()) {
            try {
                co_await it->second(channel, user, args);
            }
            catch (const std::exception& e) {
                std::cerr << "[CommandDispatcher] Handler for '"
                          << cmd << "' threw exception: " << e.what() << "\n";
            }
            co_return;
        }
        // If command not found, fall through to chat listeners.
    }

    // 6) Notify all registered ChatListener callbacks.
    for (auto& cb : chatListeners_) {
        cb(channel, user, text);
    }

    co_return;
}

} // namespace twitch_bot
