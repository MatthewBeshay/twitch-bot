#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

namespace twitch_bot {

namespace asio  = boost::asio;
namespace beast = boost::beast;
namespace ws    = beast::websocket;
namespace ssl   = asio::ssl;
using tcp       = asio::ip::tcp;

/// A coroutine-only IRC client over WebSocket + SSL.
/// Provides only asynchronous methods; do not mix any blocking calls.
class IrcClient {
public:
    /// Construct an IrcClient bound to the given I/O context and SSL context.
    /// @param ioc          The asio::io_context to drive I/O.
    /// @param ssl_ctx      The asio::ssl::context for TLS.
    /// @param oauth_token  Twitch chat OAuth token (without leading "oauth:").
    /// @param nickname     Twitch username (bot's account name).
    IrcClient(asio::io_context&  ioc,
              ssl::context&      ssl_ctx,
              std::string_view   oauth_token,
              std::string_view   nickname) noexcept;

    ~IrcClient() noexcept;

    IrcClient(const IrcClient&) = delete;
    IrcClient& operator=(const IrcClient&) = delete;

    /// Move-constructible.
    IrcClient(IrcClient&&) noexcept = default;

    /// Not move-assignable (websocket::stream is move-constructible but not move-assignable).
    IrcClient& operator=(IrcClient&&) noexcept = delete;

    /// Resolve, perform TCP→SSL→WebSocket handshake, send PASS/NICK/CAP, and JOIN channels.
    /// Steps:
    ///   1) Resolve "irc-ws.chat.twitch.tv:443"
    ///   2) Connect TCP socket
    ///   3) Perform SSL handshake
    ///   4) Perform WebSocket handshake
    ///   5) Send PASS, NICK, and CAP REQ lines
    ///   6) Join each channel in channels_to_join (no leading '#')
    /// @param channels_to_join  List of channel names (e.g. {"somechannel", "otherchannel"}).
    /// @return                  An awaitable<void> that completes after JOINs are sent.
    asio::awaitable<void> connect(std::vector<std::string> const& channels_to_join);

    /// Send one IRC line over the WebSocket (automatically appends "\r\n" if missing).
    /// @param msg  The IRC payload (e.g. "PRIVMSG #channel :hello world").
    /// @return     An awaitable<void> that completes when the frame is sent.
    asio::awaitable<void> sendLine(std::string_view msg) noexcept;

    /// Read exactly one WebSocket text frame and return its contents (without CRLF).
    /// @return An awaitable<std::string> containing the frame payload.
    asio::awaitable<std::string> readLine() noexcept;

    /// Continuously read WebSocket frames, split into IRC lines (by "\r\n"),
    /// and invoke user_callback(raw_line) for each line. Never returns unless close() is called.
    /// @param user_callback  Called with each complete IRC line (without CRLF).
    /// @return               An awaitable<void> that loops until the connection closes.
    asio::awaitable<void> readLoop(std::function<void(std::string_view)> user_callback) noexcept;

    /// Periodically send "PING :tmi.twitch.tv" every four minutes.
    /// Never returns until close() is called (which cancels the timer).
    /// @return An awaitable<void> that drives the ping loop.
    asio::awaitable<void> pingLoop() noexcept;

    /// Immediately close the underlying WebSocket/TCP socket and cancel the ping timer.
    void close() noexcept;

private:
    // Underlying Beast WebSocket-over-SSL stream (TCP → SSL → WebSocket).
    ws::stream<ssl::stream<tcp::socket>> ws_;

    // Timer for periodic PINGs.
    asio::steady_timer ping_timer_;

    // Bot credentials.
    std::string oauth_token_;
    std::string nickname_;

    // Constant for CRLF framing.
    static inline constexpr char const* CRLF = "\r\n";
};

} // namespace twitch_bot
