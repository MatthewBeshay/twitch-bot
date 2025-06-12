#include "command_dispatcher.hpp"

#include <cstring>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <iostream>

namespace twitch_bot {

CommandDispatcher::CommandDispatcher(boost::asio::any_io_executor executor) noexcept
  : executor_(std::move(executor))
{
    commandMap_.reserve(16);
    chatListeners_.reserve(4);
}

void CommandDispatcher::registerCommand(std::string_view cmd,
                                        CommandHandler handler) noexcept
{
    // store command name without leading '!'
    commandMap_.emplace(std::string(cmd), std::move(handler));
}

void CommandDispatcher::registerChatListener(ChatListener listener) noexcept
{
    chatListeners_.push_back(std::move(listener));
}

void CommandDispatcher::dispatch(const IrcMessage& msg) noexcept
{
    // only handle PRIVMSGs with at least one parameter
    if (msg.commandLen != 7 ||
        std::memcmp(msg.command, "PRIVMSG", 7) != 0 ||
        msg.paramCount < 1)
    {
        return;
    }

    std::string_view channel{ msg.params[0], msg.paramLens[0] };
    channel = normaliseChannel(channel);

    std::string_view user{ msg.prefix, msg.prefixLen };
    user = extractUser(user);

    std::string_view text{ msg.trailing, msg.trailingLen };

    if (!text.empty() && text.front() == '!') {
        // split "!cmd args" into fullCmd and args
        auto [fullCmd, args] = splitCommand(text);
        std::string_view cmdName = fullCmd.substr(1);  // drop '!'

        auto it = commandMap_.find(cmdName);
        if (it != commandMap_.end()) {
            auto handler = it->second;
            bool isModerator = msg.isModerator;

            boost::asio::co_spawn(
                executor_,
                // capture cmdName explicitly!
                [handler, channel, user, args, isModerator, cmdName]()
                -> boost::asio::awaitable<void>
                {
                    try {
                        co_await handler(channel, user, args, isModerator);
                    }
                    catch (const std::exception& e) {
                        std::cerr << "[CommandDispatcher] handler for '"
                            << cmdName << "' threw: "
                            << e.what() << '\n';
                    }
                },
                boost::asio::detached);
            return;
        }
    }

    // fallback: normal chat listeners
    for (auto& listener : chatListeners_) {
        listener(channel, user, text);
    }
}

std::string_view CommandDispatcher::normaliseChannel(
    std::string_view raw) noexcept
{
    return (!raw.empty() && raw.front() == '#')
         ? raw.substr(1)
         : raw;
}

std::string_view CommandDispatcher::extractUser(
    std::string_view prefix) noexcept
{
    auto pos = prefix.find('!');
    return (pos != std::string_view::npos)
         ? prefix.substr(0, pos)
         : prefix;
}

std::pair<std::string_view, std::string_view>
CommandDispatcher::splitCommand(std::string_view text) noexcept
{
    auto pos = text.find(' ');
    if (pos == std::string_view::npos) {
        return { text, std::string_view{} };
    }
    return { text.substr(0, pos), text.substr(pos + 1) };
}

} // namespace twitch_bot
