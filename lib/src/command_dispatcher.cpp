// C++ Standard Library
#include <iostream>

// 3rd-party
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>

// Project
#include "command_dispatcher.hpp"

namespace twitch_bot {

CommandDispatcher::CommandDispatcher(boost::asio::any_io_executor executor) noexcept
    : executor_(std::move(executor))
{
    commands_.reserve(16);
    chat_listeners_.reserve(4);
}

void CommandDispatcher::register_command(std::string_view command,
                                         command_handler_t handler) noexcept
{
    commands_.try_emplace(std::string{command}, std::move(handler));
}

void CommandDispatcher::register_chat_listener(chat_listener_t listener) noexcept
{
    chat_listeners_.push_back(std::move(listener));
}

// Run the handler and surface errors on stderr.
template <typename Handler>
boost::asio::awaitable<void> invoke_command(Handler handler, IrcMessage msg)
{
    try {
        co_await handler(std::move(msg));
    } catch (const std::exception& e) {
        std::cerr << "[dispatcher] '" << msg.command << "' threw: " << e.what() << '\n';
    }
}

void CommandDispatcher::dispatch(IrcMessage msg) noexcept
{
    // Not an IRC chat line.
    if (msg.command != "PRIVMSG" || msg.param_count < 1)
        return;

    std::string_view channel = normalize_channel(msg.params[0]);
    std::string_view user = extract_user(msg.prefix);
    std::string_view text = msg.trailing;

    // '!cmd ...'
    if (!text.empty() && text.front() == '!') {
        std::string_view cmd_name;
        std::string_view args;
        split_command(text, cmd_name, args);

        if (auto it = commands_.find(cmd_name); it != commands_.end()) {
            IrcMessage cmd_msg = msg;
            cmd_msg.command = cmd_name;
            cmd_msg.params[0] = channel;
            cmd_msg.prefix = user;
            cmd_msg.trailing = args;

            boost::asio::co_spawn(
                executor_, invoke_command(it->second, std::move(cmd_msg)), boost::asio::detached);
            return;
        }
    }

    // Plain chat line.
    for (auto& listener : chat_listeners_)
        listener(channel, user, text);
}

} // namespace twitch_bot
