#pragma once

// C++ Standard Library
#include <algorithm>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// 3rd-party
#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/thread_pool.hpp>

// Project
#include "channel_store.hpp"
#include "command_dispatcher.hpp"
#include "helix_client.hpp"
#include "irc_client.hpp"

namespace twitch_bot {

/// High-level bot tying together IRC, commands, Helix and channel storage.
/// Runs all I/O on a single strand with a pool of threads.
class TwitchBot
{
public:
    /// @pre oauth_token, client_id, client_secret and control_channel are non-empty.
    explicit TwitchBot(std::string oauth_token,
                       std::string client_id,
                       std::string client_secret,
                       std::string control_channel,
                       std::size_t threads = std::thread::hardware_concurrency());

    ~TwitchBot() noexcept;

    TwitchBot(const TwitchBot &) = delete;
    TwitchBot &operator=(const TwitchBot &) = delete;

    /// Start the bot and block until stopped.
    void run();

    /// Add a callback for every non-command chat message.
    void add_chat_listener(chat_listener_t listener);

private:
    static bool isPrivileged(const IrcMessage &msg) noexcept
    {
        if (msg.is_broadcaster)
            return true;
        if (msg.is_moderator)
            return true;
        return msg.prefix == "";
    }

    boost::asio::awaitable<void> run_bot() noexcept;

    static constexpr std::string_view CRLF{"\r\n"};

    boost::asio::thread_pool pool_;
    boost::asio::strand<boost::asio::any_io_executor> strand_;
    boost::asio::ssl::context ssl_ctx_;

    const std::string oauth_token_;
    const std::string client_id_;
    const std::string client_secret_;
    const std::string control_channel_;

    IrcClient irc_client_;
    CommandDispatcher dispatcher_;
    HelixClient helix_client_;
    ChannelStore channel_store_;
};

} // namespace twitch_bot
