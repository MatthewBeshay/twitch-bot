// twitch_bot.hpp
#pragma once

#include "irc_socket.hpp"
#include "faceit_client.hpp"
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <string_view>
#include <vector>
#include <functional>
#include <unordered_map>
#include <optional>
#include <chrono>
#include <thread>

// A tiny parsed‐IRC struct:
struct IrcMessage {
    std::unordered_map<std::string_view, std::string_view> tags;
    std::string_view prefix;     // e.g. ":foo!foo@foo.tmi.twitch.tv"
    std::string_view command;    // e.g. "PRIVMSG", "CLEARCHAT", "NOTICE", etc.
    std::vector<std::string_view> params;   // space-separated params (e.g. "#chan", "user")
    std::string_view trailing;   // the “:rest of the line” portion
};

namespace asio = boost::asio;

/// Twitch chat bot (IRC→WebSocket+SSL) with FACEIT & Helix integrations.
class TwitchBot {
public:
    using ChatListener = std::function<void(
        std::string_view channel,
        std::string_view user,
        std::string_view message
    )>;

    explicit TwitchBot(std::string oauthToken,
                       std::string clientId,
                       std::string clientSecret,
                       std::string controlChannel,
                       std::string faceitApiKey);

    ~TwitchBot() noexcept;

    /// Connects, pings, reads chat, blocks until shutdown.
    void run();

    /// Subscribe to every parsed chat message.
    void addChatListener(ChatListener cb);

private:
    // — Event loop & I/O —
    void connectWebSocket();
    void schedulePing();
    void readLoop();

    // — IRC framing & thread‐safe writes —
    void sendRaw(std::string_view msg) noexcept;
    void joinChannel(std::string_view channel) noexcept;
    void leaveChannel(std::string_view channel) noexcept;

    // — Parsing & dispatch —
    void onMessage(std::string_view raw);
    void onPrivMsg(std::string_view channel, IrcMessage const& msg);
    void handleCommand(std::string_view channel,
                       std::string_view user,
                       std::string_view content);

    // — Commands —
    void cmdJoin(std::string_view user) noexcept;
    void cmdLeave(std::string_view user) noexcept;
    void cmdSetNickname(std::string_view channel,
                        std::string_view nickname) noexcept;
    void cmdRecord(std::string_view channel, int limit);
    void cmdRank(std::string_view channel);

    // — Helix API helpers —
    void ensureHelixToken();
    std::optional<std::chrono::milliseconds>
    getStreamStart(std::string_view channel);

    // — Persistence —
    void loadChannels();
    void saveChannels();

    // — State —
    asio::io_context                                  ioc_;
    asio::ssl::context                                sslCtx_;
    IrcSocket                                         socket_;
    asio::steady_timer                                pingTimer_;

    const std::string                                 oauthToken_;
    const std::string                                 clientId_;
    const std::string                                 clientSecret_;
    const std::string                                 controlChannel_;

    faceit::Client                                    faceitClient_;

    std::string                                       helixToken_;
    std::chrono::steady_clock::time_point             helixExpiry_;

    std::unordered_map<std::string,std::optional<std::string>>
                                                      channelNicks_;
    std::thread                                       readerThread_;
    std::vector<ChatListener>                         chatListeners_;
};
