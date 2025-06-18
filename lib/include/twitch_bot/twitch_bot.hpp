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
#include "faceit_client.hpp"
#include "helix_client.hpp"
#include "irc_client.hpp"
#include "utils/attributes.hpp"

namespace twitch_bot {

/// Coordinates IRC, commands, Helix queries and channel storage.
/// All asynchronous work is serialised through \p strand_, executed by \p pool_.
class TwitchBot
{
public:
    /// Create a bot; \p threads controls the size of the worker pool.
    /// Pre: \p oauth_token, \p client_id, \p client_secret and \p control_channel are non-empty.
    explicit TwitchBot(std::string oauth_token,
                       std::string client_id,
                       std::string client_secret,
                       std::string control_channel,
                       std::string faceit_api_key,
                       std::size_t threads = std::thread::hardware_concurrency());

    ~TwitchBot() noexcept;

    TwitchBot(const TwitchBot&) = delete;
    TwitchBot& operator=(const TwitchBot&) = delete;

    /// Run until the IO context stops.
    void run();

    /// Register a listener for non-command chat messages.
    void add_chat_listener(chat_listener_t listener);

private:
    /// Broadcaster, moderator or internal server message.
    /// Broadcaster, moderator or internal server message.
    TB_FORCE_INLINE
    static bool isPrivileged(const IrcMessage& msg) noexcept
    {
        if (msg.is_broadcaster)
            return true;
        if (msg.is_moderator)
            return true;
        return msg.prefix == ""; // BOT CREATOR'S TWITCH USERNAME
    }

    boost::asio::awaitable<void> run_bot() noexcept;

    static constexpr std::string_view CRLF{"\r\n"}; ///< IRC line terminator

    boost::asio::thread_pool pool_;
    boost::asio::strand<boost::asio::any_io_executor> strand_;
    boost::asio::ssl::context ssl_ctx_;

    const std::string oauth_token_;
    const std::string client_id_;
    const std::string client_secret_;
    const std::string control_channel_;
    const std::string faceit_api_key_;

    IrcClient irc_client_;
    CommandDispatcher dispatcher_;
    HelixClient helix_client_;
    ChannelStore channel_store_;
    faceit::Client faceit_client_;
};

} // namespace twitch_bot
