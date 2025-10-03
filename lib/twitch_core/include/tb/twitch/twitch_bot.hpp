/*
Module Name:
- twitch_bot.hpp

Abstract:
- High level Twitch bot that wires IRC, command dispatch, Helix and channel management.
- All async work is serialised on strand_ and executed by a thread pool.
- Exposes small safe helpers for chat that respect Twitch 500 byte limits.

Why:
- Serialising on a strand keeps message ordering deterministic and avoids races across handlers.
- A shared SSL context and executor let downstream code reuse connection pools and timers.
*/
#pragma once

// C++ Standard Library
#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Boost.Asio
#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/thread_pool.hpp>

// Core
#include "command_dispatcher.hpp"
#include "helix_client.hpp"
#include "irc_client.hpp"
#include <tb/parser/irc_message_parser.hpp>
#include <tb/utils/attributes.hpp>

namespace twitch_bot
{

    // Coordinates IRC, commands, Helix queries and channel storage.
    // All asynchronous work is serialized through strand_, executed by pool_.
    class TwitchBot
    {
    public:
        // Pre: access_token, refresh_token, client_id, client_secret and control_channel are non-empty.
        explicit TwitchBot(std::string access_token,
                           std::string refresh_token,
                           std::string client_id,
                           std::string client_secret,
                           std::string control_channel,
                           std::size_t threads = std::thread::hardware_concurrency());

        ~TwitchBot() noexcept;

        TwitchBot(const TwitchBot&) = delete;
        TwitchBot& operator=(const TwitchBot&) = delete;

        // Run until the IO context stops.
        void run();

        // Register a listener for non-command chat messages.
        void add_chat_listener(chat_listener_t listener);

        // Access the command dispatcher to register app-level commands.
        [[nodiscard]] CommandDispatcher& dispatcher() noexcept
        {
            return dispatcher_;
        }

        // Helix client (for stream status, etc.).
        [[nodiscard]] HelixClient& helix() noexcept
        {
            return helix_client_;
        }

        // Executor and SSL so app code can build its own HTTP clients.
        [[nodiscard]] boost::asio::any_io_executor executor() const noexcept
        {
            return strand_.get_inner_executor();
        }
        [[nodiscard]] boost::asio::ssl::context& ssl_context() noexcept
        {
            return ssl_ctx_;
        }

        // Control channel name.
        [[nodiscard]] std::string_view control_channel() const noexcept
        {
            return control_channel_;
        }

        // Set channels to auto-join on (re)connect. No core persistence.
        void set_initial_channels(std::vector<std::string> channels);

        // Runtime join and part.
        [[nodiscard]] boost::asio::awaitable<void> join_channel(std::string_view channel);
        [[nodiscard]] boost::asio::awaitable<void> part_channel(std::string_view channel);

        // Safe chat helpers: wrap to 500 bytes and sanitise CR or LF.
        boost::asio::awaitable<void> say(std::string_view channel, std::string_view text);
        boost::asio::awaitable<void>
        reply(std::string_view channel, std::string_view parent_msg_id, std::string_view text);

        // Privilege check: broadcaster or moderator via parsed flags.
        // Treat empty prefix as an internal admin signal.
        [[nodiscard]] bool is_privileged(const IrcMessage& msg) const noexcept
        {
            if (msg.is_broadcaster)
            {
                return true;
            }
            if (msg.is_moderator)
            {
                return true;
            }
            return msg.prefix == ""; // admin
        }

    private:
        boost::asio::awaitable<void> run_bot();

        static constexpr std::string_view kCRLF{ "\r\n" }; // line terminator

        boost::asio::thread_pool pool_; // worker threads
        boost::asio::strand<boost::asio::any_io_executor> strand_; // serialises callbacks
        boost::asio::ssl::context ssl_ctx_; // shared TLS config

        const std::string access_token_;
        const std::string refresh_token_;
        const std::string client_id_;
        const std::string client_secret_;
        const std::string control_channel_;

        IrcClient irc_client_;
        CommandDispatcher dispatcher_;
        HelixClient helix_client_;

        std::mutex chan_mutex_; // protects initial_channels_
        std::vector<std::string> initial_channels_;
    };

} // namespace twitch_bot
