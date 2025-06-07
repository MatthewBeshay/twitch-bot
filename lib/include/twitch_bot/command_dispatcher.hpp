#pragma once

#include "message_parser.hpp"
#include "utils/transparent_string.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

#include <boost/asio/awaitable.hpp>

namespace twitch_bot {

using ChannelName = std::string;

/// Called on every PRIVMSG: (channel, user, message).
using ChatListener = std::function<void(std::string_view channel,
                                        std::string_view user,
                                        std::string_view message)>;

/// Called when a registered command (for example, "!join") is invoked.
/// Returns an awaitable<void> that will be co_awaited by the dispatcher.
using CommandHandler = std::function<
    boost::asio::awaitable<void>(
        std::string_view channel,
        std::string_view user,
        std::string_view args,
        const std::unordered_map<std::string_view, std::string_view>& tags
    )
>;

/**
 * @brief Inspects each IrcMessage. If it is a PRIVMSG starting with '!',
 *        parses out the command and arguments, looks up a registered handler,
 *        and co_awaits it. Otherwise, notifies all registered ChatListener callbacks.
 */
class CommandDispatcher {
public:
    CommandDispatcher() = default;
    ~CommandDispatcher() = default;

    // Non-copyable, non-movable
    CommandDispatcher(const CommandDispatcher&) = delete;
    CommandDispatcher& operator=(const CommandDispatcher&) = delete;

    /**
     * @brief Register a handler for a specific command (including the leading '!').
     *
     * Example:
     *   registerCommand("!join", [](auto ch, auto user, auto args) { ... });
     *
     * @param cmd      The command name (with leading '!'), e.g. "!join".
     * @param handler  The coroutine to invoke when that command is seen.
     */
    void registerCommand(std::string_view cmd, CommandHandler handler)
    {
        // Store a std::string key, but allow lookup by string_view.
        commandMap_.emplace(std::string(cmd), std::move(handler));
    }

    /**
     * @brief Register a generic chat listener invoked on every PRIVMSG,
     *        whether it's a command or plain text.
     *
     * @param listener  Called with (channel, user, message) for each PRIVMSG.
     */
    void registerChatListener(ChatListener listener)
    {
        chatListeners_.push_back(std::move(listener));
    }

    /**
     * @brief Dispatch a parsed IrcMessage. If it is a PRIVMSG and the message
     *        text starts with '!', splits it into command + args, looks up a handler
     *        (using heterogeneous lookup), and co_awaits it. Otherwise, calls all
     *        ChatListener callbacks.
     *
     * @param msg  An IrcMessage (tags/prefix/command/params/trailing) parsed earlier.
     */
    boost::asio::awaitable<void> dispatch(const IrcMessage& msg);

private:
    // Map from command string to handler. Uses TransparentStringHash/Eq
    // for zero-overhead lookup by string_view, const char*, or std::string.
    std::unordered_map<
        std::string,
        CommandHandler,
        TransparentStringHash,
        TransparentStringEq
    > commandMap_;

    // List of callbacks for plain chat messages (or fallback if no command matches).
    std::vector<ChatListener> chatListeners_;
};

} // namespace twitch_bot
