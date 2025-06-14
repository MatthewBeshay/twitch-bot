// C++ Standard Library
#include <iostream>

// 3rd-party
#include <boost/asio/connect.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/awaitable.hpp>
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
                     boost::asio::ssl::context&     ssl_context,
                     std::string_view               oauth_token,
                     std::string_view               nickname) noexcept
    : ws_stream_{boost::asio::make_strand(executor), ssl_context}
    , ping_timer_{executor}
    , read_buffer_{}
    , oauth_token_{oauth_token}
    , nickname_{nickname}
{
}

boost::asio::awaitable<void>
IrcClient::connect(std::span<const std::string_view> channels) noexcept
{
    static constexpr char host_name[] = "irc-ws.chat.twitch.tv";
    static constexpr char port_str[]  = "443";

    // Resolve and connect TCP
    auto executor = co_await boost::asio::this_coro::executor;
    boost::asio::ip::tcp::resolver resolver{executor};
    auto endpoints = co_await resolver.async_resolve(
        host_name, port_str, use_awaitable);
    co_await boost::asio::async_connect(
        boost::beast::get_lowest_layer(ws_stream_),
        endpoints,
        use_awaitable);

    // Perform TLS handshake and set SNI
    SSL_set_tlsext_host_name(
        ws_stream_.next_layer().native_handle(),
        host_name);
    co_await ws_stream_.next_layer().async_handshake(
        boost::asio::ssl::stream_base::client,
        use_awaitable);

    // Perform WebSocket handshake
    ws_stream_.set_option(
        boost::beast::websocket::stream_base::timeout::
        suggested(boost::beast::role_type::client));
    ws_stream_.set_option(
        boost::beast::websocket::stream_base::decorator(
        [](boost::beast::websocket::request_type& req)
        {
            req.set(boost::beast::http::field::user_agent,
                    "TwitchBot");
        }));
    co_await ws_stream_.async_handshake(
        host_name, "/", use_awaitable);

    // Authenticate
    {
        std::array<const_buffer, 2> bufs{
            buffer("PASS ", 5),
            buffer(oauth_token_)
        };
        co_await send_buffers(bufs);
    }
    {
        std::array<const_buffer, 2> bufs{
            buffer("NICK ", 5),
            buffer(nickname_)
        };
        co_await send_buffers(bufs);
    }

    // Request tags, commands and membership
    {
        static constexpr char caps[] =
            "CAP REQ :twitch.tv/membership "
            "twitch.tv/tags twitch.tv/commands\r\n";
        std::array<const_buffer, 1> bufs{
            buffer(caps, sizeof(caps) - 1)
        };
        co_await send_buffers(bufs);
    }

    // Join channels
    for (auto channel : channels)
    {
        std::array<const_buffer, 5> bufs{
            buffer("JOIN", 4),
            buffer(" ", 1),
            buffer("#", 1),
            buffer(channel),
            buffer(k_crlf.data(), k_crlf.size())
        };
        co_await send_buffers(bufs);
    }
}

boost::asio::awaitable<void>
IrcClient::send_line(std::string_view message) noexcept
{
    std::array<const_buffer, 2> bufs{
        buffer(message.data(), message.size()),
        buffer(k_crlf.data(), k_crlf.size())
    };
    co_await send_buffers(bufs);
}

boost::asio::awaitable<void>
IrcClient::send_buffers(
    std::span<const boost::asio::const_buffer> buffers) noexcept
{
    try
    {
        ws_stream_.text(true);
        co_await ws_stream_.async_write(buffers, use_awaitable);
    }
    catch (...)
    {
        // ignore errors
    }
}

boost::asio::awaitable<void, boost::asio::any_io_executor>
IrcClient::ping_loop() noexcept
{
    for (;;)
    {
        ping_timer_.expires_after(std::chrono::minutes{4});
        co_await ping_timer_.async_wait(use_awaitable);
        co_await send_line("PING :tmi.twitch.tv");
    }
}

void IrcClient::close() noexcept
{
    // Stop ping loop
    ping_timer_.cancel();

    // Close WebSocket and TLS
    boost::asio::post(ws_stream_.get_executor(),
        [this]()
        {
            ws_stream_.async_close(
                boost::beast::websocket::close_code::normal,
                [](error_code){});
            ws_stream_.next_layer().async_shutdown(
                [](error_code){});
        });
}

} // namespace twitch_bot
