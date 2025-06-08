#pragma once

#include "command_dispatcher.hpp"
#include "irc_client.hpp"
#include "helix_client.hpp"
#include "channel_store.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <span>
#include <memory>
#include <thread>
#include <algorithm>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>

namespace twitch_bot {

/// High‐level bot tying together IRC, commands, Helix and channel storage.
/// Runs on a pool of threads for fully non‐blocking I/O.
class TwitchBot {
public:
    TwitchBot(std::string        oauthToken,
              std::string        clientId,
              std::string        clientSecret,
              std::string        controlChannel,
              std::size_t        threads = std::thread::hardware_concurrency());

    ~TwitchBot() noexcept;

    TwitchBot(const TwitchBot&) = delete;
    TwitchBot& operator=(const TwitchBot&) = delete;

    /// Start the bot and block until stopped.
    void run();

    /// Register a listener for every non-command chat message.
    void addChatListener(ChatListener cb);

private:
    boost::asio::awaitable<void> runBot();

    boost::asio::io_context      ioc_;
    boost::asio::ssl::context    ssl_ctx_;
    const std::string            controlChannel_;

    IrcClient             ircClient_;
    CommandDispatcher     dispatcher_;
    HelixClient           helixClient_;
    ChannelStore          channelStore_;

    const std::size_t     threadCount_;
    std::vector<std::thread> threads_;

    const std::string     oauthToken_;
    const std::string     clientId_;
    const std::string     clientSecret_;
};

} // namespace twitch_bot
