#pragma once

#include "message_parser.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

#include <boost/asio/awaitable.hpp>

namespace twitch_bot {

/// Called on every PRIVMSG (channel, user, message).
using ChatListener = std::function<void(std::string_view channel,
                                        std::string_view user,
                                        std::string_view message)>;

/// Called when a registered command (e.g. "!join") is invoked. Returns awaitable<void>.
using CommandHandler = std::function<
    boost::asio::awaitable<void>(std::string_view channel,
                                  std::string_view user,
                                  std::string_view args)>;

/// Hash functor for string_view that is transparent (for heterogeneous lookup).
struct StringViewHash {
    using is_transparent = void;
    std::size_t operator()(std::string_view v) const noexcept {
        return std::hash<std::string_view>{}(v);
    }
};

/// Equality functor for string_view that is transparent (for heterogeneous lookup).
struct StringViewEq {
    using is_transparent = void;
    bool operator()(std::string_view a, std::string_view b) const noexcept {
        return a == b;
    }
};

/**
 * @brief CommandDispatcher inspects each IrcMessage. If it's a PRIVMSG and the
 *        trailing text starts with '!', it tries to match a registered command
 *        handler. Otherwise, it notifies all registered ChatListener callbacks.
 */
class CommandDispatcher {
public:
    CommandDispatcher() = default;
    ~CommandDispatcher() = default;

    // non-copyable, non-movable
    CommandDispatcher(const CommandDispatcher&) = delete;
    CommandDispatcher& operator=(const CommandDispatcher&) = delete;

    /**
     * @brief Register a handler for a specific command (including leading '!').
     *        Example: registerCommand("!join", [](auto ch, auto user, auto args){...});
     *
     * @param cmd      The command name (with leading '!'), e.g. "!join".
     * @param handler  The coroutine to invoke when that command is seen.
     */
    void registerCommand(std::string_view cmd, CommandHandler handler) {
        commandMap_.emplace(std::string(cmd), std::move(handler));
    }

    /**
     * @brief Register a generic chat listener invoked on every PRIVMSG (command or not).
     *
     * @param listener  Called with (channel, user, message) for each PRIVMSG.
     */
    void registerChatListener(ChatListener listener) {
        chatListeners_.push_back(std::move(listener));
    }

    /**
     * @brief Dispatch a parsed IrcMessage. If it's PRIVMSG and starts with '!',
     *        split into command + args, look up a handler (via heterogeneous lookup),
     *        and co_await it. Otherwise, call all ChatListener callbacks.
     *
     * @param msg  An IrcMessage (tags/prefix/command/params/trailing) parsed earlier.
     */
    boost::asio::awaitable<void> dispatch(IrcMessage const& msg);

private:
    // Store commands in an unordered_map keyed by std::string, but allow lookup by std::string_view.
    std::unordered_map<
        std::string,
        CommandHandler,
        StringViewHash,
        StringViewEq
    > commandMap_;

    // List of callbacks for plain chat messages (or fallback if no command matches).
    std::vector<ChatListener> chatListeners_;
};

} // namespace twitch_bot
