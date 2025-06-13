#include "command_dispatcher.hpp"

#include <iostream>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>

namespace twitch_bot {

CommandDispatcher::CommandDispatcher(boost::asio::any_io_executor executor) noexcept
  : executor_(std::move(executor))
{
    commands_.reserve(16);
    chat_listeners_.reserve(4);
}

void CommandDispatcher::registerCommand(std::string_view command,
                                        CommandHandler  handler) noexcept
{
    commands_.emplace(std::string{command}, std::move(handler));
}

void CommandDispatcher::registerChatListener(ChatListener listener) noexcept
{
    chat_listeners_.push_back(std::move(listener));
}

void CommandDispatcher::dispatch(const IrcMessage& message) noexcept
{
    // Fast-reject non-PRIVMSG or missing params
    if (message.command != "PRIVMSG" ||
        message.param_count < 1)
    {
        return;
    }

    // Normalize channel and extract user
    auto const channel = normalizeChannel(message.params[0]);
    auto const user    = extractUser(message.prefix);

    // Check for '!' to decide between command vs chat
    auto const text = message.trailing;
    if (!text.empty() && text.front() == '!') {
        // Inline split
        std::string_view cmdName, args;
        splitCommand(text, cmdName, args);

        // Transparent lookup (no allocations on find)
        if (auto it = commands_.find(cmdName);
            it != commands_.end())
        {
            bool const isMod = message.is_moderator;
            auto const handler = it->second;

            boost::asio::co_spawn(
                executor_,
                [handler, channel, user, args, isMod, cmdName]() 
                -> boost::asio::awaitable<void>
                {
                    try {
                        co_await handler(channel, user, args, isMod);
                    } catch (std::exception const& e) {
                        std::cerr << "[Dispatcher] '" << cmdName
                                  << "' threw: " << e.what() << "\n";
                    }
                },
                boost::asio::detached);
            return;
        }
    }

    // Fallback: regular chat listeners
    for (auto const& listener : chat_listeners_) {
        listener(channel, user, text);
    }
}

} // namespace twitch_bot
