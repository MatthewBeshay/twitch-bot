#pragma once

#include "command_dispatcher.hpp"
#include "irc_client.hpp"
#include "helix_client.hpp"
#include "channel_store.hpp"
#include "faceit_client.hpp"

#include <string>
#include <vector>
#include <functional>
#include <memory>

#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>

namespace twitch_bot {

/**
 * @brief The "glue" class that ties together:
 *   - IrcClient        (network + IRC framing)
 *   - CommandDispatcher (dispatch commands + chat callbacks)
 *   - HelixClient      (OAuth + Helix calls)
 *   - ChannelStore     (persist alias + FACEIT nick per channel)
 *   - faceit::Client   (FACEIT Data API)
 *
 * Public API:
 *   - TwitchBot(oauthToken, clientId, clientSecret, controlChannel, faceitApiKey)
 *   - ~TwitchBot()
 *   - void run()                     // blocks until termination
 *   - void addChatListener(cb)       // register "on chat" callbacks
 */
class TwitchBot {
public:
    /**
     * @brief Construct a TwitchBot.
     *
     * @param oauthToken       Raw OAuth token for Twitch chat (no leading "oauth:").
     * @param clientId         Twitch App client ID (Helix).
     * @param clientSecret     Twitch App client secret (Helix).
     * @param controlChannel   Channel where bot listens for control commands.
     * @param faceitApiKey     FACEIT Data API key (v4).
     */
    TwitchBot(std::string oauthToken,
              std::string clientId,
              std::string clientSecret,
              std::string controlChannel,
              std::string faceitApiKey);

    ~TwitchBot() noexcept;

    TwitchBot(const TwitchBot&) = delete;
    TwitchBot& operator=(const TwitchBot&) = delete;

    /**
     * @brief Start the bot:
     *   1) Load channels (alias+faceit) from disk
     *   2) co_spawn runBot()
     *   3) block in ioc_.run() until shutdown
     */
    void run();

    /**
     * @brief Register a chat listener (called on every PRIVMSG that isn't a simple bot command).
     * Internally this calls dispatcher_->registerChatListener(cb).
     */
    void addChatListener(ChatListener cb);

private:
    /// The main coroutine: connects, spawns ping/read loops, then idles forever.
    boost::asio::awaitable<void> runBot();

    // --- Underlying components ---
    boost::asio::io_context             ioc_;
    boost::asio::ssl::context           ssl_ctx_;

    std::unique_ptr<IrcClient>          ircClient_;       ///< IRC over WebSocket+TLS
    std::unique_ptr<CommandDispatcher>  dispatcher_;      ///< parse & dispatch commands
    std::unique_ptr<HelixClient>        helixClient_;     ///< Helix REST calls
    std::unique_ptr<ChannelStore>       channelStore_;    ///< alias + FACEIT nick persistence
    std::unique_ptr<faceit::Client>     faceitClient_;    ///< FACEIT Data API client

    // Immutable configuration:
    const std::string                    oauthToken_;
    const std::string                    clientId_;
    const std::string                    clientSecret_;
    const std::string                    controlChannel_;
};

} // namespace twitch_bot
