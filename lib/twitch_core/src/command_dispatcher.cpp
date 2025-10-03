/*
Module Name:
- command_dispatcher.cpp

Abstract:
- Routes chat to either a command coroutine or plain listeners.

Why:
- Pre-reserve small buckets to avoid rehash churn on first use.
- Contain exceptions inside command coroutines so a bad handler cannot tear down the bot.
- Copy the target handler into the coroutine so it stays valid even if the map changes.
*/

// C++ Standard Library
#include <iostream>

// Boost.Asio
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>

// Core
#include <tb/twitch/command_dispatcher.hpp>

namespace twitch_bot
{

    CommandDispatcher::CommandDispatcher(boost::asio::any_io_executor executor) :
        executor_(std::move(executor))
    {
        commands_.reserve(16); // small stable footprint for a handful of commands
        chat_listeners_.reserve(4); // avoid first-push reallocs in hot paths
    }

    void CommandDispatcher::register_command(std::string_view command, command_handler_t handler)
    {
        // Insert-or-ignore by design: accidental duplicate registration is treated as a no-op.
        // If you prefer last-wins, switch to operator[] assignment.
        (void)commands_.try_emplace(std::string{ command }, std::move(handler));
    }

    void CommandDispatcher::register_chat_listener(chat_listener_t listener)
    {
        chat_listeners_.push_back(std::move(listener));
    }

    // Run the handler and surface errors without crashing the event loop.
    template<typename Handler>
    boost::asio::awaitable<void> invoke_command(Handler handler, IrcMessage msg)
    {
        try
        {
            co_await handler(std::move(msg));
        }
        catch (const std::exception& e)
        {
            std::cerr << "[dispatcher] '" << msg.command << "' threw: " << e.what() << '\n';
        }
        catch (...)
        {
            std::cerr << "[dispatcher] '" << msg.command << "' threw: <unknown exception>\n";
        }
    }

    // Route a single line.
    // Prefer command handling first so chat listeners do not double-handle command text.
    void CommandDispatcher::route_text(std::string_view channel,
                                       std::string_view user,
                                       std::string_view text,
                                       std::string_view raw_tags,
                                       bool is_moderator,
                                       bool is_broadcaster)
    {
        if (!text.empty() && text.front() == '!')
        {
            std::string_view cmd_name;
            std::string_view args;
            split_command(text, cmd_name, args);
            if (auto it = commands_.find(cmd_name); it != commands_.end())
            {
                IrcMessage cmd_msg{};
                cmd_msg.command = cmd_name;
                cmd_msg.params[0] = channel;
                cmd_msg.param_count = 1;
                cmd_msg.prefix = user;
                cmd_msg.trailing = args;
                cmd_msg.raw_tags = raw_tags; // forward tags for context-sensitive handlers
                cmd_msg.is_moderator = is_moderator ? 1 : 0; // keep role bits
                cmd_msg.is_broadcaster = is_broadcaster ? 1 : 0;

                // Copy the target functor into the coroutine so it cannot dangle if the map mutates.
                boost::asio::co_spawn(
                    executor_, invoke_command(it->second, std::move(cmd_msg)), boost::asio::detached);
                return;
            }
        }

        // Not a command or no matching handler: notify listeners.
        for (auto& listener : chat_listeners_)
            listener(channel, user, text);
    }

    void CommandDispatcher::dispatch_text(std::string_view channel,
                                          std::string_view user,
                                          std::string_view text)
    {
        // No tags available in this entry point.
        route_text(channel, user, text);
    }

    void CommandDispatcher::dispatch(IrcMessage msg)
    {
        // Only chat lines are interesting here.
        if (msg.command != "PRIVMSG" || msg.param_count < 1)
        {
            return;
        }

        std::string_view channel = Normalise_channel(msg.params[0]);
        std::string_view user = extract_user(msg.prefix);
        std::string_view text = msg.trailing;

        // Preserve tags and role bits so permission checks can happen in handlers.
        route_text(channel, user, text, msg.raw_tags, msg.is_moderator != 0, msg.is_broadcaster != 0);
    }

} // namespace twitch_bot
