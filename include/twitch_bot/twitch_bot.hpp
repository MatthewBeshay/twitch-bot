#pragma once

#include "faceit_client.hpp"
#include "irc_socket.hpp"

#include <chrono>
#include <functional>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/json.hpp>

namespace asio = boost::asio;
namespace json = boost::json;

/// A tiny parsed-IRC struct.
struct IrcMessage
{
    std::unordered_map<std::string_view, std::string_view> tags;
    std::string_view                                      prefix;   // e.g. ":foo!foo@foo.tmi.twitch.tv"
    std::string_view                                      command;  // e.g. "PRIVMSG"
    std::vector<std::string_view>                         params;
    std::string_view                                      trailing; // the “:rest of the line”
};

/// Twitch chat bot (IRC->WebSocket+SSL) with FACEIT & Helix integrations.
class TwitchBot
{
public:
    using ChatListener = std::function<void(std::string_view channel,
                                            std::string_view user,
                                            std::string_view message)>;

    /// @param oauthToken       OAuth token for Twitch chat (PASS)
    /// @param clientId         Twitch App Client ID (Helix)
    /// @param clientSecret     Twitch App Client Secret (Helix)
    /// @param controlChannel   Channel where bot listens for control commands
    /// @param faceitApiKey     FACEIT Data API key (v4)
    explicit TwitchBot(std::string oauthToken,
                       std::string clientId,
                       std::string clientSecret,
                       std::string controlChannel,
                       std::string faceitApiKey);
    ~TwitchBot() noexcept;

    /// Start the bot; this will block in run() until shutdown.
    void run();

    /// Register a callback for every received chat message.
    void addChatListener(ChatListener cb);

private:
    //
    // Top-level coroutine
    //
    asio::awaitable<void> runBot();

    //
    // Connection & event loops
    //
    asio::awaitable<void> connectWebSocket();
    asio::awaitable<void> schedulePing();
    asio::awaitable<void> readLoop();

    //
    // IRC framing & writes
    //
    asio::awaitable<void> sendRaw(std::string_view msg) noexcept;
    asio::awaitable<void> joinChannel(std::string_view channel) noexcept;
    asio::awaitable<void> leaveChannel(std::string_view channel) noexcept;

    //
    // Parsing & dispatch
    //
    static IrcMessage     parseIrc(std::string_view line);
    asio::awaitable<void> onMessage(std::string_view raw);
    asio::awaitable<void> onPrivMsg(std::string_view   channel,
                                    IrcMessage const& m);
    asio::awaitable<void> handleCommand(std::string_view channel,
                                        std::string_view user,
                                        std::string_view content);

    //
    // Commands
    //
    asio::awaitable<void> cmdJoin(std::string_view user) noexcept;
    asio::awaitable<void> cmdLeave(std::string_view user) noexcept;
    asio::awaitable<void> cmdSetNickname(std::string_view channel,
                                         std::string_view nickname) noexcept;
    asio::awaitable<void> cmdRecord(std::string_view channel,
                                    int               limit);
    asio::awaitable<void> cmdRank(std::string_view channel);

    //
    // Helix API helpers
    //
    asio::awaitable<void>                               ensureHelixToken();
    asio::awaitable<std::optional<std::chrono::milliseconds>>
                                                        getStreamStart(std::string_view channel);

    //
    // Persistence
    //
    void loadChannels();
    void saveChannels();

    //
    // State
    //
    asio::io_context                                        ioc_;
    asio::ssl::context                                      sslCtx_;
    IrcSocket                                               socket_;
    asio::steady_timer                                      pingTimer_;

    const std::string                                       oauthToken_;
    const std::string                                       clientId_;
    const std::string                                       clientSecret_;
    const std::string                                       controlChannel_;

    faceit::Client                                          faceitClient_;

    std::string                                             helixToken_;
    std::chrono::steady_clock::time_point                   helixExpiry_;

    std::unordered_map<std::string, std::optional<std::string>>
                                                           channelNicks_;
    std::vector<ChatListener>                               chatListeners_;
};
