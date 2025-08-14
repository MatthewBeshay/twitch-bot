// C++ Standard Library
#include <iostream>
#include <string>

// Boost.asio
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

// Boost.system
#include <boost/system/error_code.hpp>

// OpenSSL
#include <openssl/ssl.h>

// Project
#include <tb/twitch/irc_client.hpp>

namespace twitch_bot {

using boost::asio::buffer;
using boost::asio::const_buffer;
using boost::asio::use_awaitable;
using error_code = boost::system::error_code;

IrcClient::IrcClient(boost::asio::any_io_executor executor,
                     boost::asio::ssl::context& ssl_context,
                     std::string_view access_token,
                     std::string_view nickname)
    : ws_stream_{boost::asio::make_strand(executor), ssl_context}
    , ping_timer_{executor}
    , access_token_{access_token}
    , nickname_{nickname}
{
}

// Resolve, connect, upgrade to TLS and WebSocket, then join channels.
auto IrcClient::connect(std::span<const std::string_view> channels) -> boost::asio::awaitable<void>
{
    static char host_name[] = "irc-ws.chat.twitch.tv";
    static char port_str[] = "443";

    // TCP connect
    auto executor = co_await boost::asio::this_coro::executor;
    boost::asio::ip::tcp::resolver resolver{executor};
    auto endpoints = co_await resolver.async_resolve(host_name, port_str, use_awaitable);
    co_await boost::asio::async_connect(
        boost::beast::get_lowest_layer(ws_stream_), endpoints, use_awaitable);

    // TLS handshake + SNI
    if (!::SSL_set_tlsext_host_name(ws_stream_.next_layer().native_handle(), host_name)) {
        // Surface as a stream error; users can catch around connect()
        throw std::system_error{static_cast<int>(::ERR_get_error()),
                                boost::asio::error::get_ssl_category(), "SNI failure"};
    }
    co_await ws_stream_.next_layer().async_handshake(boost::asio::ssl::stream_base::client,
                                                     use_awaitable);

    // WebSocket handshake
    ws_stream_.set_option(
        boost::beast::websocket::stream_base::timeout::suggested(boost::beast::role_type::client));
    ws_stream_.set_option(boost::beast::websocket::stream_base::decorator(
        [](boost::beast::websocket::request_type& req) {
            req.set(boost::beast::http::field::user_agent, "TwitchBot");
        }));
    co_await ws_stream_.async_handshake(host_name, "/", use_awaitable);

    // PASS <access_token>
    {
        std::array<const_buffer, 3> bufs_pass{buffer("PASS ", 5), buffer(access_token_),
                                              boost::asio::buffer(kCRLF)};
        co_await send_buffers(bufs_pass);
    }

    // NICK <nickname>
    {
        std::array<const_buffer, 3> bufs_nick{buffer("NICK ", 5), buffer(nickname_),
                                              boost::asio::buffer(kCRLF)};
        co_await send_buffers(bufs_nick);
    }

    // Negotiate IRCv3 capabilities.
    {
        static constexpr char caps[]
            = "CAP REQ :twitch.tv/membership twitch.tv/tags twitch.tv/commands\r\n";
        std::array<const_buffer, 1> bufs{buffer(caps, sizeof(caps) - 1)};
        co_await send_buffers(bufs);
    }

    // JOIN #<channel>
    for (auto channel : channels) {
        std::array<const_buffer, 5> bufs{buffer("JOIN", 4), buffer(" ", 1), buffer("#", 1),
                                         buffer(channel), boost::asio::buffer(kCRLF)};
        co_await send_buffers(bufs);
    }
}

// Send one IRC line.
auto IrcClient::send_line(std::string_view message) noexcept -> boost::asio::awaitable<void>
{
    std::array<const_buffer, 2> bufs{buffer(message.data(), message.size()),
                                     boost::asio::buffer(kCRLF)};
    co_await send_buffers(bufs);
}

// Write buffers as a text WebSocket frame.
auto IrcClient::send_buffers(std::span<const boost::asio::const_buffer> buffers) noexcept
    -> boost::asio::awaitable<void>
{
    try {
        ws_stream_.text(true);
        co_await ws_stream_.async_write(buffers, use_awaitable);
    } catch (...) {
        // silent drop - connection close will surface elsewhere
    }
}

// Convenience: PRIVMSG helper. Channel must not include '#'.
auto IrcClient::privmsg(std::string_view channel, std::string_view text) noexcept
    -> boost::asio::awaitable<void>
{
    // "PRIVMSG #", channel, " :", text, "\r\n"
    std::array<const_buffer, 5> bufs{buffer("PRIVMSG #", 9), buffer(channel), buffer(" :", 2),
                                     buffer(text), boost::asio::buffer(kCRLF)};
    co_await send_buffers(bufs);
}

// Convenience: threaded reply using @reply-parent-msg-id.
// If parent_msg_id is empty, falls back to a normal PRIVMSG.
auto IrcClient::reply(std::string_view channel,
                      std::string_view parent_msg_id,
                      std::string_view text) noexcept -> boost::asio::awaitable<void>
{
    if (parent_msg_id.empty()) {
        co_await privmsg(channel, text);
        co_return;
    }

    // "@reply-parent-msg-id=", id, " PRIVMSG #", channel, " :", text, "\r\n"
    std::array<const_buffer, 7> bufs{buffer("@reply-parent-msg-id=", 21),
                                     buffer(parent_msg_id),
                                     buffer(" PRIVMSG #", 10),
                                     buffer(channel),
                                     buffer(" :", 2),
                                     buffer(text),
                                     boost::asio::buffer(kCRLF)};
    co_await send_buffers(bufs);
}

// --------- helpers for wrapped sending ---------

void IrcClient::sanitize_crlf(std::string& s) noexcept
{
    for (char& c : s) {
        if (c == '\r' || c == '\n')
            c = ' ';
    }
}

std::size_t IrcClient::utf8_clip_len(std::string_view s, std::size_t max_bytes) noexcept
{
    if (s.size() <= max_bytes)
        return s.size();
    std::size_t i = max_bytes;
    // back up to the first non-continuation byte (check i-1, not i)
    while (i > 0 && (static_cast<unsigned char>(s[i - 1]) & 0xC0) == 0x80)
        --i;
    return i;
}

std::size_t IrcClient::utf8_chunk_by_words(std::string_view s,
                                           std::size_t start,
                                           std::size_t max_bytes) noexcept
{
    const std::size_t remaining = s.size() - start;
    const std::size_t hard = utf8_clip_len(s.substr(start), std::min(max_bytes, remaining));
    if (hard == 0)
        return 0;

    std::size_t end = start + hard;
    // Prefer to break on last space before hard boundary
    for (std::size_t i = end; i > start; --i) {
        if (s[i - 1] == ' ') {
            end = i - 1;
            break;
        }
    }
    if (end == start)
        end = start + hard; // fallback
    return end - start;
}

// Send long text split across multiple PRIVMSGs (UTF-8 safe, 500 bytes each)
auto IrcClient::privmsg_wrap(std::string_view channel, std::string_view text) noexcept
    -> boost::asio::awaitable<void>
{
    std::string body{text};
    sanitize_crlf(body);

    std::size_t pos = 0;
    while (pos < body.size()) {
        const std::size_t len = utf8_chunk_by_words(body, pos, kMaxChatBytes);
        const std::string_view chunk{body.data() + pos, len};

        std::array<const_buffer, 5> bufs{buffer("PRIVMSG #", 9), buffer(channel), buffer(" :", 2),
                                         buffer(chunk), boost::asio::buffer(kCRLF)};
        co_await send_buffers(bufs);

        pos += len;
        while (pos < body.size() && body[pos] == ' ')
            ++pos; // drop leading spaces
    }
}

// Wrap & send as threaded replies (each chunk replies to same parent)
auto IrcClient::reply_wrap(std::string_view channel,
                           std::string_view parent_msg_id,
                           std::string_view text) noexcept -> boost::asio::awaitable<void>
{
    if (parent_msg_id.empty()) {
        co_await privmsg_wrap(channel, text);
        co_return;
    }

    std::string body{text};
    sanitize_crlf(body);

    std::size_t pos = 0;
    while (pos < body.size()) {
        const std::size_t len = utf8_chunk_by_words(body, pos, kMaxChatBytes);
        const std::string_view chunk{body.data() + pos, len};

        std::array<const_buffer, 7> bufs{buffer("@reply-parent-msg-id=", 21),
                                         buffer(parent_msg_id),
                                         buffer(" PRIVMSG #", 10),
                                         buffer(channel),
                                         buffer(" :", 2),
                                         buffer(chunk),
                                         boost::asio::buffer(kCRLF)};
        co_await send_buffers(bufs);

        pos += len;
        while (pos < body.size() && body[pos] == ' ')
            ++pos;
    }
}

// Keep the connection alive with PING every four minutes.
auto IrcClient::ping_loop() -> boost::asio::awaitable<void, boost::asio::any_io_executor>
{
    for (;;) {
        ping_timer_.expires_after(std::chrono::minutes{4});
        co_await ping_timer_.async_wait(use_awaitable);
        co_await send_line("PING :tmi.twitch.tv");
    }
}

void IrcClient::close() noexcept
{
    ping_timer_.cancel();

    boost::asio::post(ws_stream_.get_executor(), [this] {
        ws_stream_.async_close(boost::beast::websocket::close_code::normal, [](error_code) { });
        ws_stream_.next_layer().async_shutdown([](error_code) { });
    });
}

} // namespace twitch_bot
