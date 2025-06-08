#include "command_dispatcher.hpp"

#include <iostream>

#include <boost/asio/use_awaitable.hpp>

namespace twitch_bot {

CommandDispatcher::CommandDispatcher(
    boost::asio::any_io_executor executor) noexcept
  : executor_(std::move(executor))
{
    // Expect a small number of commands and listeners.
    commandMap_.reserve(16);
    chatListeners_.reserve(4);
}

void CommandDispatcher::registerCommand(
    std::string_view cmd,
    CommandHandler handler) noexcept
{
    // Store the full command (including ’!’) as the key.
    commandMap_.emplace(std::string(cmd), std::move(handler));
}

void CommandDispatcher::registerChatListener(
    ChatListener listener) noexcept
{
    chatListeners_.push_back(std::move(listener));
}

void CommandDispatcher::dispatch(
    IrcMessage const& msg) noexcept
{
    // Only care about PRIVMSG with at least one parameter.
    if (msg.command != "PRIVMSG" || msg.params.empty()) {
        return;
    }

    auto channel = normaliseChannel(msg.params[0]);
    auto user    = extractUser(msg.prefix);
    auto text    = msg.trailing;

    // If it’s a command, spawn its handler as a coroutine.
    if (!text.empty() && text.front() == '!') {
        auto [cmd, args] = splitCommand(text);
        auto it = commandMap_.find(cmd);
        if (it != commandMap_.end()) {
            auto handler = it->second;
            boost::asio::co_spawn(
                executor_,
                [=]() -> boost::asio::awaitable<void> {
                    try {
                        co_await handler(channel, user, args, msg.tags);
                    } catch (std::exception const& e) {
                        std::cerr << "[CommandDispatcher] handler for '"
                                  << cmd << "' threw: " << e.what() << '\n';
                    }
                },
                boost::asio::detached);
            return;
        }
    }

    // Not a recognised command – invoke all chat listeners synchronously.
    for (auto const& listener : chatListeners_) {
        listener(channel, user, text);
    }
}

std::string_view CommandDispatcher::normaliseChannel(
    std::string_view raw) noexcept
{
    // Remove Twitch IRC ’#’ prefix if present.
    if (!raw.empty() && raw.front() == '#') {
        return raw.substr(1);
    }
    return raw;
}

std::string_view CommandDispatcher::extractUser(
    std::string_view prefix) noexcept
{
    if (prefix.empty()) {
        return prefix;
    }
    // Prefix format is ’user!ident@host’
    auto pos = prefix.find('!');
    if (pos != std::string_view::npos) {
        return prefix.substr(0, pos);
    }
    return prefix;
}

std::pair<std::string_view, std::string_view> CommandDispatcher::splitCommand(
    std::string_view text) noexcept
{
    // Split at first space: "!cmd args..."
    auto pos = text.find(' ');
    if (pos != std::string_view::npos) {
        return { text.substr(0, pos), text.substr(pos + 1) };
    }
    return { text, std::string_view{} };
}

} // namespace twitch_bot
