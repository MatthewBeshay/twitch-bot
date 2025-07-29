// C++ Standard Library
#include <iostream>

// 3rd-party
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/error_code.hpp>

#include <openssl/ssl.h>

// Project
#include "irc_client.hpp"

namespace twitch_bot {

using boost::asio::buffer;
using boost::asio::const_buffer;
using boost::asio::use_awaitable;
using error_code = boost::system::error_code;

IrcClient::IrcClient(boost::asio::any_io_executor executor,
                     boost::asio::ssl::context& ssl_context,
                     std::string_view oauth_token,
                     std::string_view nickname) noexcept
    : ws_stream_{boost::asio::make_strand(executor), ssl_context}
    , ping_timer_{executor}
    , oauth_token_{oauth_token}
    , nickname_{nickname}
{
}

/// Resolve, connect, upgrade to TLS and WebSocket, then join channels.
auto IrcClient::connect(std::span<const std::string_view> channels) noexcept
    -> boost::asio::awaitable<void>
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
    SSL_set_tlsext_host_name(ws_stream_.next_layer().native_handle(), host_name);
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

    // PASS <oauth_token>
    {
        static constexpr std::string_view pass_cmd{"PASS "};
        std::array<const_buffer, 3> bufs_pass{buffer("PASS ", 5), buffer(oauth_token_),
                                              boost::asio::buffer(kCRLF)};
        co_await send_buffers(bufs_pass);
    }

    // NICK <nickname>
    {
        std::array<const_buffer, 3> bufs_nick{buffer("NICK ", 5), buffer(nickname_),
                                              boost::asio::buffer(kCRLF)};
        co_await send_buffers(bufs_nick);
    }

    // Negotiate IRCv3 capabilities:
    // ‑ membership (JOIN/PART events for all users)
    // ‑ tags       (message metadata like badges, emotes, colours)
    // ‑ commands   (Twitch‑specific notices and user events)
    {
        static constexpr char caps[]
            = "CAP REQ :twitch.tv/membership twitch.tv/tags twitch.tv/commands\r\n";
        std::array<const_buffer, 1> bufs{buffer(caps, sizeof(caps) - 1)};
        co_await send_buffers(bufs);
    }

    // JOIN #<channel>
    for (auto channel : channels) {
        static constexpr std::string_view join_cmd{"JOIN"};
        static constexpr std::string_view space{" "};
        static constexpr std::string_view hash{"#"};
        std::array<const_buffer, 5> bufs{
            buffer(join_cmd.data(), join_cmd.size()), buffer(space.data(), space.size()),
            buffer(hash.data(), hash.size()), buffer(channel), boost::asio::buffer(kCRLF)};
        co_await send_buffers(bufs);
    }
}

/// Send one IRC line.
auto IrcClient::send_line(std::string_view message) noexcept -> boost::asio::awaitable<void>
{
    std::array<const_buffer, 2> bufs{buffer(message.data(), message.size()),
                                     boost::asio::buffer(kCRLF)};
    co_await send_buffers(bufs);
}

/// Write buffers as a text WebSocket frame.
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

/// Keep the connection alive with PING every four minutes.
auto IrcClient::ping_loop() noexcept -> boost::asio::awaitable<void, boost::asio::any_io_executor>
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
