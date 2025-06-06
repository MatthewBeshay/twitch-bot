#pragma once

#include "twitch_bot/command_dispatcher.hpp"
#include "twitch_bot/irc_client.hpp"
#include "twitch_bot/helix_client.hpp"
#include "twitch_bot/channel_store.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/steady_timer.hpp>

namespace twitch_bot {

/// Glue class that ties together:
///   - IrcClient          (network + IRC framing)
///   - CommandDispatcher  (dispatch commands + chat callbacks)
///   - HelixClient        (OAuth + Helix calls)
///   - ChannelStore       (load/save channel list)
///
/// Public API:
///   - TwitchBot(oauthToken, clientId, clientSecret, controlChannel)
///   - ~TwitchBot()
///   - void run()                   // blocks until termination
///   - void addChatListener(cb)     // register 'on chat' callbacks
class TwitchBot {
public:
    /// Construct a TwitchBot.
    /// @param oauthToken      OAuth token for Twitch chat (PASS).
    /// @param clientId        Twitch App client ID (Helix).
    /// @param clientSecret    Twitch App client secret (Helix).
    /// @param controlChannel  Channel where bot listens for control commands.
    TwitchBot(std::string oauthToken,
              std::string clientId,
              std::string clientSecret,
              std::string controlChannel);

    ~TwitchBot() noexcept;

    TwitchBot(const TwitchBot&) = delete;
    TwitchBot& operator=(const TwitchBot&) = delete;

    /// Start the bot. This will:
    ///   1) Load channels from disk
    ///   2) co_spawn runBot()
    ///   3) block in ioc_.run() until shutdown
    void run();

    /// Register a chat listener (called on every PRIVMSG that is not a bot command).
    /// Internally this calls dispatcher_.registerChatListener(cb).
    void addChatListener(ChatListener cb);

private:
    /// Main coroutine: connects, spawns ping/read loops, then idles forever.
    boost::asio::awaitable<void> runBot();

    // Underlying components
    boost::asio::io_context           ioc_;
    boost::asio::ssl::context         ssl_ctx_;

    std::unique_ptr<IrcClient>        ircClient_;
    std::unique_ptr<CommandDispatcher> dispatcher_;
    std::unique_ptr<HelixClient>      helixClient_;
    std::unique_ptr<ChannelStore>     channelStore_;

    // Immutable configuration
    const std::string                  oauthToken_;
    const std::string                  clientId_;
    const std::string                  clientSecret_;
    const std::string                  controlChannel_;
};

} // namespace twitch_bot
