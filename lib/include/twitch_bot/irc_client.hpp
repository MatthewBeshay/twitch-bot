#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <functional>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/steady_timer.hpp>

#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

namespace twitch_bot {
namespace asio  = boost::asio;
namespace beast = boost::beast;
namespace ws    = beast::websocket;
namespace ssl   = asio::ssl;
using tcp       = asio::ip::tcp;

/**
 * \brief A coroutine-only IRC client over WebSocket + SSL.
 *
 * Provides only asynchronous methods (C++20 coroutines). Do not mix
 * any blocking calls with these methods; use coroutines exclusively.
 */
class IrcClient {
public:
    /**
     * \brief Construct an IrcClient bound to the given I/O context and SSL context.
     * \param ioc          The boost::asio::io_context to drive I/O.
     * \param ssl_ctx      The boost::asio::ssl::context for TLS.
     * \param oauth_token  Twitch chat OAuth token (without leading "oauth:").
     * \param nickname     Twitch username (bot's Username).
     */
    IrcClient(asio::io_context& ioc,
              ssl::context&     ssl_ctx,
              std::string_view  oauth_token,
              std::string_view  nickname) noexcept;

    ~IrcClient() noexcept;

    IrcClient(const IrcClient&) = delete;
    IrcClient& operator=(const IrcClient&) = delete;

    /// Move-constructible
    IrcClient(IrcClient&&) noexcept = default;

    /// Not move-assignable (websocket::stream is move-constructible but not move-assignable)
    IrcClient& operator=(IrcClient&&) noexcept = delete;

    /**
     * \brief Resolve, TCP->SSL->WebSocket handshake, then send PASS/NICK/CAP and JOIN each channel.
     *
     * Steps:
     *   1) DNS resolve "irc-ws.chat.twitch.tv:443"
     *   2) TCP connect the underlying socket
     *   3) Perform SSL handshake
     *   4) Perform WebSocket handshake
     *   5) Send PASS, NICK, CAP REQ lines
     *   6) JOIN each channel in channelsToJoin (each without leading '#')
     *
     * \param channelsToJoin  List of channel names (e.g. {"somechannel", "otherchannel"}).
     * \return An awaitable<void> that completes after all JOINs are sent.
     */
    asio::awaitable<void> connect(std::vector<std::string> const& channelsToJoin);

    /**
     * \brief Send one IRC line over the WebSocket (automatically appends "\r\n" if missing).
     * \param msg  The IRC payload (e.g. "PRIVMSG #channel :hello world").
     * \return An awaitable<void> that completes when the frame is sent.
     */
    asio::awaitable<void> sendLine(std::string_view msg) noexcept;

    /**
     * \brief Read exactly one WebSocket text frame and return its contents.
     * \return An awaitable<std::string> containing the frame payload (without the CRLF).
     */
    asio::awaitable<std::string> readLine() noexcept;

    /**
     * \brief Continuously read WebSocket frames, split into IRC lines (by "\r\n"),
     *        and invoke userCallback(rawLine) for each line. Never returns unless close() is called.
     *
     * \param userCallback  Called with each complete IRC line (without CRLF).
     * \return              An awaitable<void> that loops until the connection closes.
     */
    asio::awaitable<void> readLoop(std::function<void(std::string_view)> userCallback) noexcept;

    /**
     * \brief Periodically send "PING :tmi.twitch.tv" every 4 minutes.
     *        Never returns until close() is called (which cancels the timer).
     *
     * \return An awaitable<void> that drives the ping loop.
     */
    asio::awaitable<void> pingLoop() noexcept;

    /**
     * \brief Immediately close the underlying WebSocket/TCP socket and cancel the ping timer.
     */
    void close() noexcept;

private:
    // Underlying Beast WebSocket-over-SSL stream (TCP -> SSL -> WebSocket)
    ws::stream<ssl::stream<tcp::socket>>        ws_;

    // Timer for periodic PINGs
    asio::steady_timer                          ping_timer_;

    // Bot credentials
    std::string                                 oauth_token_;
    std::string                                 nickname_;

    // Constant for CRLF framing
    static inline constexpr char const* CRLF = "\r\n";
};

} // namespace twitch_bot
