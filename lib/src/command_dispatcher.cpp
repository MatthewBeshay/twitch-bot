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

void CommandDispatcher::dispatch(IrcMessage msg) noexcept
{
    // ignore non-PRIVMSG or missing parameters
    if (msg.command != "PRIVMSG" || msg.param_count < 1) {
        return;
    }

    // extract channel and user
    std::string_view channel = normalize_channel(msg.params[0]);
    std::string_view user = extract_user(msg.prefix);
    std::string_view text = msg.trailing;

    // handle commands prefixed with '!'
    if (!text.empty() && text.front() == '!') {
        std::string_view cmd_name;
        std::string_view args;
        split_command(text, cmd_name, args);

        auto it = commands_.find(cmd_name);
        if (it != commands_.end()) {
            // prepare message for handler
            IrcMessage cmd_msg = msg;
            cmd_msg.command = cmd_name;
            cmd_msg.params[0] = channel;
            cmd_msg.prefix = user;
            cmd_msg.trailing = args;

            auto handler = it->second;
            boost::asio::co_spawn(
                executor_,
                [handler, cmd_msg = std::move(cmd_msg)]() mutable -> boost::asio::awaitable<void> {
                    try {
                        co_await handler(cmd_msg);
                    } catch (std::exception const &e) {
                        std::cerr << "[dispatcher] '" << cmd_msg.command << "' threw: " << e.what()
                                  << "\n";
                    }
                },
                boost::asio::detached);
            return;
        }
    }

    // notify chat listeners
    for (auto const &listener : chat_listeners_) {
        listener(channel, user, text);
    }
}

} // namespace twitch_bot
