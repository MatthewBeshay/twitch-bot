// command_dispatcher.hpp
#pragma once

#include "message_parser.hpp"
#include "utils/transparent_string.hpp"

#include <string>
#include <string_view>
#include <functional>
#include <vector>
#include <unordered_map>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

namespace twitch_bot {

/// @brief Called on every PRIVMSG (channel, user, text).
using ChatListener = std::function<void(std::string_view channel,
                                        std::string_view user,
                                        std::string_view text)>;

/// @brief Called when a command is seen. Runs asynchronously on the supplied executor.
/// @param channel     Channel name (no leading ‘#’)
/// @param user        User name (before the ‘!’ in the prefix)
/// @param args        Arguments string (after the command)
/// @param isModerator True if @mod=1 was present on the message
using CommandHandler = std::function<boost::asio::awaitable<void>(
    std::string_view,
    std::string_view,
    std::string_view,
    bool
)>;

/// @brief Dispatches PRIVMSGs to registered command handlers or chat listeners.
///        Normal chat runs synchronously; commands (leading ‘!’) are spawned as coroutines.
class CommandDispatcher {
public:
    /// @param executor Asio executor to run command coroutines on.
    explicit CommandDispatcher(boost::asio::any_io_executor executor) noexcept;

    CommandDispatcher(const CommandDispatcher&) = delete;
    CommandDispatcher& operator=(const CommandDispatcher&) = delete;
    ~CommandDispatcher() = default;

    /// @brief Register a handler for “!cmd” (without the ‘!’). Must be done before dispatch().
    void registerCommand(std::string_view cmd,
                         CommandHandler handler) noexcept;

    /// @brief Register a fallback listener for non-command chat messages.
    void registerChatListener(ChatListener listener) noexcept;

    /// @brief Dispatch a parsed IRC message. Returns immediately.
    void dispatch(const IrcMessage& msg) noexcept;

private:
    /// @brief Strip leading ‘#’ from a channel name.
    static inline std::string_view normaliseChannel(std::string_view raw) noexcept;

    /// @brief Extract the user portion (before ‘!’) from the prefix.
    static inline std::string_view extractUser(std::string_view prefix) noexcept;

    /// @brief Split text of form “!cmd args” → { "!cmd", "args" }.
    static inline std::pair<std::string_view, std::string_view>
    splitCommand(std::string_view text) noexcept;

    boost::asio::any_io_executor executor_;
    std::unordered_map<std::string,
                       CommandHandler,
                       TransparentStringHash,
                       TransparentStringEq> commandMap_;
    std::vector<ChatListener> chatListeners_;
};

} // namespace twitch_bot
