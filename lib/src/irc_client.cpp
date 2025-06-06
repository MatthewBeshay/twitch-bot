#include "irc_client.hpp"

#include <array>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>

#include <openssl/ssl.h>

namespace twitch_bot {

namespace asio  = boost::asio;
namespace beast = boost::beast;
namespace ws    = beast::websocket;
namespace ssl   = asio::ssl;
using tcp       = asio::ip::tcp;
using error_code = boost::system::error_code;

IrcClient::IrcClient(asio::io_context& ioc,
                     ssl::context&     ssl_ctx,
                     std::string_view  oauth_token,
                     std::string_view  nickname) noexcept
    : ws_{asio::make_strand(ioc.get_executor()), ssl_ctx}
    , ping_timer_{ioc}
    , oauth_token_{oauth_token}
    , nickname_{nickname}
{
}

IrcClient::~IrcClient() noexcept
{
    // Close the underlying socket if open, then cancel the ping timer.
    error_code ec;
    auto& socket = beast::get_lowest_layer(ws_);
    if (socket.is_open()) {
        socket.close(ec);
    }
    ping_timer_.cancel();
}

asio::awaitable<void> IrcClient::connect(
    std::vector<std::string> const& channels_to_join)
{
    constexpr char const* host = "irc-ws.chat.twitch.tv";
    constexpr char const* port = "443";

    // 1) Get this coroutine's executor.
    auto executor = co_await asio::this_coro::executor;

    // 2) Resolve the hostname.
    tcp::resolver resolver(executor);
    auto endpoints = co_await resolver.async_resolve(
        host, port, asio::use_awaitable);

    // 3) Connect the raw TCP socket.
    co_await asio::async_connect(
        beast::get_lowest_layer(ws_),
        endpoints,
        asio::use_awaitable);

    // 4) Perform SSL handshake with SNI.
    if (!SSL_set_tlsext_host_name(
            ws_.next_layer().native_handle(),
            host))
    {
        // SNI setup failed; abort connect.
        co_return;
    }
    co_await ws_.next_layer().async_handshake(
        ssl::stream_base::client,
        asio::use_awaitable);

    // 5) Perform the WebSocket handshake.
    ws_.set_option(ws::stream_base::timeout::suggested(
        beast::role_type::client));
    ws_.set_option(ws::stream_base::decorator(
        [](ws::request_type& req) {
            req.set(beast::http::field::user_agent, "TwitchBot");
        }));
    co_await ws_.async_handshake(host, "/", asio::use_awaitable);

    // 6) Send PASS, NICK, and CAP REQ lines.
    {
        std::string pass_line = "PASS " + oauth_token_ + "\r\n";
        co_await ws_.async_write(
            asio::buffer(pass_line),
            asio::use_awaitable);

        std::string nick_line = "NICK " + nickname_ + "\r\n";
        co_await ws_.async_write(
            asio::buffer(nick_line),
            asio::use_awaitable);

        std::string cap_req =
            "CAP REQ :twitch.tv/membership twitch.tv/tags twitch.tv/commands\r\n";
        co_await ws_.async_write(
            asio::buffer(cap_req),
            asio::use_awaitable);
    }

    // 7) Join each channel (prepend '#').
    for (auto const& channel : channels_to_join) {
        std::string join_line = "JOIN #" + channel + "\r\n";
        co_await ws_.async_write(
            asio::buffer(join_line),
            asio::use_awaitable);
    }
}

asio::awaitable<void> IrcClient::sendLine(std::string_view msg) noexcept
{
    try {
        ws_.text(true);

        // Send [msg] followed by CRLF in a two-buffer write.
        constexpr char const* crlf = "\r\n";
        auto buf1 = asio::buffer(msg);
        auto buf2 = asio::buffer(crlf, 2);
        std::array<asio::const_buffer, 2> buffers{{ buf1, buf2 }};
        co_await ws_.async_write(buffers, asio::use_awaitable);
    }
    catch (const std::exception& e) {
        std::cerr << "[IrcClient] sendLine error: " << e.what() << "\n";
    }
}

asio::awaitable<std::string> IrcClient::readLine() noexcept
{
    beast::flat_buffer buffer;
    co_await ws_.async_read(buffer, asio::use_awaitable);
    co_return beast::buffers_to_string(buffer.data());
}

asio::awaitable<void> IrcClient::readLoop(
    std::function<void(std::string_view)> user_callback) noexcept
{
    beast::flat_buffer buffer;

    for (;;) {
        // Read one WebSocket frame into the buffer.
        co_await ws_.async_read(buffer, asio::use_awaitable);

        // Convert buffer contents into a string_view.
        auto data = buffer.data();
        std::string_view chunk{
            static_cast<const char*>(data.data()),
            data.size()
        };
        buffer.consume(buffer.size());

        // Split chunk on "\r\n" and invoke callback for each line.
        size_t pos = 0;
        while (pos < chunk.size()) {
            auto next = chunk.find("\r\n", pos);
            auto line = chunk.substr(
                pos,
                (next == std::string_view::npos)
                    ? (chunk.size() - pos)
                    : (next - pos));
            pos = (next == std::string_view::npos)
                ? chunk.size()
                : (next + 2);

            if (!line.empty()) {
                user_callback(line);
            }
        }
    }
}

asio::awaitable<void> IrcClient::pingLoop() noexcept
{
    for (;;) {
        ping_timer_.expires_after(std::chrono::minutes(4));
        co_await ping_timer_.async_wait(asio::use_awaitable);
        co_await sendLine("PING :tmi.twitch.tv");
    }
}

void IrcClient::close() noexcept
{
    // 1) Send WebSocket close frame.
    error_code ec_close;
    ws_.close(ws::close_code::normal, ec_close);

    // 2) Perform SSL shutdown (ignore truncated errors).
    error_code ec_shutdown;
    ws_.next_layer().shutdown(ec_shutdown);
    if (ec_shutdown && ec_shutdown != ssl::error::stream_truncated) {
        std::cerr << "[IrcClient] SSL shutdown error: "
                  << ec_shutdown.message() << "\n";
    }

    // 3) Cancel the ping timer.
    ping_timer_.cancel();
}

} // namespace twitch_bot
