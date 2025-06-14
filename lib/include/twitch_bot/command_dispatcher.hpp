// command_dispatcher.hpp
#pragma once

#include "message_parser.hpp"  // Defines IrcMessage
#include "utils/transparent_string.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// 3rd-party
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

// Project
#include "utils/attributes.hpp"
#include "utils/transparent_string.hpp"

#include "message_parser.hpp"

namespace twitch_bot {

/// Called on every PRIVMSG (channel, user, message).
using ChatListener = std::function<void(std::string_view channel,
                                        std::string_view user,
                                        std::string_view message)>;

/// Called when a registered command (e.g. "!join") is invoked.
/// The `tags` map contains IRC tags from the incoming message.
using CommandHandler = std::function<
    boost::asio::awaitable<void>(std::string_view                                         channel,
                                  std::string_view                                         user,
                                  std::string_view                                         args,
                                  const std::unordered_map<std::string_view,std::string_view>& tags)>;

/**
 * @brief CommandDispatcher inspects each IrcMessage. If it's a PRIVMSG and the
 *        trailing text starts with '!', it tries to match a registered command
 *        handler. Otherwise, it notifies all registered ChatListener callbacks.
 */
class CommandDispatcher {
public:
    // Construct with an executor for spawning handlers.
    explicit CommandDispatcher(boost::asio::any_io_executor executor) noexcept;

    // Register a command and its handler.
    void register_command(std::string_view command, command_handler_t handler) noexcept;

    // Register a fallback chat listener.
    void register_chat_listener(chat_listener_t listener) noexcept;

    /**
     * @brief Register a handler for a specific command (including leading '!').
     *        Example: registerCommand("!join", [](auto ch, auto user, auto args, auto& tags){ ... });
     *
     * @param cmd      The command name (with leading '!'), e.g. "!join".
     * @param handler  The coroutine to invoke when that command is seen.
     */
    void registerCommand(std::string_view cmd, CommandHandler handler) {
        // We store std::string(cmd) as the key; heterogeneous lookup is enabled.
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
     *        and co_await it (passing along msg.tags). Otherwise, call all
     *        ChatListener callbacks.
     *
     * @param msg  An IrcMessage (tags/prefix/command/params/trailing) parsed earlier.
     */
    boost::asio::awaitable<void> dispatch(IrcMessage const& msg);

private:
    // Store commands in an unordered_map keyed by std::string,
    // but allow lookup by std::string_view without allocations.
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
