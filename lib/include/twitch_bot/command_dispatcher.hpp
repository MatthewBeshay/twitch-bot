#pragma once

#include "message_parser.hpp"
#include "utils/transparent_string.hpp"

#include <string>
#include <string_view>
#include <functional>
#include <vector>
#include <unordered_map>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>

namespace twitch_bot {

/// Invoked on every PRIVMSG (channel, user, text).
using ChatListener = std::function<void(std::string_view channel,
                                        std::string_view user,
                                        std::string_view text)>;

/// Invoked when a command is seen. Runs asynchronously on the supplied executor.
/// Parameters: channel, user, args, tags.
using CommandHandler = std::function<boost::asio::awaitable<void>(
    std::string_view,
    std::string_view,
    std::string_view,
    TagList const&)>;

/// Dispatches PRIVMSGs to either registered command handlers or chat listeners.
/// Uses a purely synchronous path for normal chat to avoid coroutine-frame overhead.
/// Commands (leading ’!’) are spawned as coroutines on the given executor.
class CommandDispatcher {
public:
    /// @param executor  Asio executor used to run command coroutines.
    explicit CommandDispatcher(boost::asio::any_io_executor executor) noexcept;

    ~CommandDispatcher() = default;
    CommandDispatcher(const CommandDispatcher&) = delete;
    CommandDispatcher& operator=(const CommandDispatcher&) = delete;

    /// Register a handler for a command (e.g. ’!foo’). Must be done before dispatch.
    void registerCommand(std::string_view cmd,
                         CommandHandler handler) noexcept;

    /// Register a fallback listener for non-command chat.
    void registerChatListener(ChatListener listener) noexcept;

    /// Dispatch a parsed IRC message. Returns immediately.
    void dispatch(IrcMessage const& msg) noexcept;

private:
    /// Remove leading ’#’ from a channel name.
    static inline std::string_view normaliseChannel(
        std::string_view raw) noexcept;

    /// Extract the user portion before the ’!’ in the prefix.
    static inline std::string_view extractUser(
        std::string_view prefix) noexcept;

    /// Split text of form ’!cmd args…’ into { "!cmd", "args…" }.
    static inline std::pair<std::string_view, std::string_view> splitCommand(
        std::string_view text) noexcept;

    boost::asio::any_io_executor executor_;
    std::unordered_map<std::string,
                       CommandHandler,
                       TransparentStringHash,
                       TransparentStringEq> commandMap_;
    std::vector<ChatListener> chatListeners_;
};

} // namespace twitch_bot
