/*
Module Name:
- command_dispatcher.hpp

Abstract:
- Routes parsed IRC messages and plain chat lines to command handlers.
- Handlers run on a supplied Asio executor to keep call sites thread agnostic.
- Commands are case sensitive and keyed without allocations via transparent hashing.
*/
#pragma once

// C++ Standard Library
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Boost.Asio
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

// Core
#include <tb/parser/irc_message_parser.hpp>
#include <tb/utils/attributes.hpp>
#include <tb/utils/transparent_string_hash.hpp>

namespace twitch_bot
{

    // Plain chat listener for non-command lines.
    using chat_listener_t = std::function<void(std::string_view channel, std::string_view user, std::string_view text)>;

    // Coroutine handler for an IRC command.
    using command_handler_t = std::function<boost::asio::awaitable<void>(IrcMessage msg)>;

    // Routes IRC messages to command handlers or chat listeners.
    class CommandDispatcher
    {
    public:
        // Work is posted onto executor so dispatch() can be called from any thread.
        explicit CommandDispatcher(boost::asio::any_io_executor executor);

        // Register a handler for 'command' (case sensitive). Later calls replace earlier ones.
        void register_command(std::string_view command, command_handler_t handler);

        // Register a fallback listener for non-command chat lines.
        void register_chat_listener(chat_listener_t listener);

        // Dispatch a raw chat line. Channel should not include '#'; user is the login name.
        // Useful when upstream did not keep the full IRC prefix.
        void dispatch_text(std::string_view channel, std::string_view user, std::string_view text);

        // Dispatch a parsed IRC message.
        void dispatch(IrcMessage msg);

    private:
        // Keep channel keys uniform - most code expects names without '#'.
        static TB_FORCE_INLINE std::string_view Normalise_channel(std::string_view raw) noexcept
        {
            return (!raw.empty() && raw.front() == '#') ? raw.substr(1) : raw;
        }

        // Fast path to the login part in "login!ident@host".
        static TB_FORCE_INLINE std::string_view extract_user(std::string_view prefix) noexcept
        {
            auto pos = prefix.find('!');
            return (pos != std::string_view::npos) ? prefix.substr(0, pos) : prefix;
        }

        // Avoid heap work when splitting a potential command; cheap checks first.
        static TB_FORCE_INLINE void split_command(std::string_view text,
                                                  std::string_view& out_cmd,
                                                  std::string_view& out_args) noexcept
        {
            if (text.size() > 1 && text.front() == '!')
            {
                auto pos = text.find(' ');
                if (pos == std::string_view::npos)
                {
                    out_cmd = text.substr(1);
                    out_args = {};
                }
                else
                {
                    out_cmd = text.substr(1, pos - 1);
                    out_args = text.substr(pos + 1);
                }
            }
            else
            {
                out_cmd = {};
                out_args = {};
            }
        }

        boost::asio::any_io_executor executor_;
        std::unordered_map<std::string,
                           command_handler_t,
                           TransparentBasicStringHash<char>,
                           TransparentBasicStringEq<char>>
            commands_;
        std::vector<chat_listener_t> chat_listeners_;

        // Single routing point so both IRC and raw-chat paths share behaviour.
        // raw_tags and role flags are optional; supply when available to avoid re-parsing.
        void route_text(std::string_view channel,
                        std::string_view user,
                        std::string_view text,
                        std::string_view raw_tags = {},
                        bool is_moderator = false,
                        bool is_broadcaster = false);
    };

} // namespace twitch_bot
