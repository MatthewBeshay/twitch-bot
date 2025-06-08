#include "irc_client.hpp"

#include <array>
#include <iostream>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/co_spawn.hpp>

#include <boost/beast/websocket/stream.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/beast/core/role.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>

#include <openssl/ssl.h>

namespace twitch_bot {

    using boost::asio::use_awaitable;
    using boost::asio::buffer;
    using boost::asio::const_buffer;
    using boost::beast::websocket::close_code;
    using error_code = boost::system::error_code;

    IrcClient::IrcClient(boost::asio::any_io_executor exec,
        boost::asio::ssl::context& ssl_ctx,
        std::string_view              oauth_token,
        std::string_view              nickname) noexcept
        : ws_{ boost::asio::make_strand(exec), ssl_ctx }
        , ping_timer_{ exec }
        , read_buffer_()
        , oauth_token_{ oauth_token }
        , nickname_{ nickname }
    {
    }


    boost::asio::awaitable<void> IrcClient::connect(
        std::span<const std::string_view> channels) noexcept
    {
        constexpr char host[] = "irc-ws.chat.twitch.tv";
        constexpr char port[] = "443";

        // 1) DNS + TCP
        auto exec = co_await boost::asio::this_coro::executor;
        boost::asio::ip::tcp::resolver res(exec);
        auto eps = co_await res.async_resolve(host, port, use_awaitable);
        co_await boost::asio::async_connect(
            boost::beast::get_lowest_layer(ws_), eps, use_awaitable);

        // 2) TLS handshake + SNI
        SSL_set_tlsext_host_name(
            ws_.next_layer().native_handle(), host);
        co_await ws_.next_layer().async_handshake(
            boost::asio::ssl::stream_base::client, use_awaitable);

        // 3) WebSocket handshake
        ws_.set_option(boost::beast::websocket::stream_base::timeout::
            suggested(boost::beast::role_type::client));
        ws_.set_option(boost::beast::websocket::stream_base::decorator(
            [](boost::beast::websocket::request_type& req) {
                req.set(boost::beast::http::field::user_agent,
                    "TwitchBot");
            }));
        co_await ws_.async_handshake(host, "/", use_awaitable);

        // 4) Authenticate
        {
            std::array<const_buffer, 2> bufs{ {
                buffer("PASS ",5),
                buffer(oauth_token_)
            } };
            co_await sendBuffers(bufs);
        }
        {
            std::array<const_buffer, 2> bufs{ {
                buffer("NICK ",5),
                buffer(nickname_)
            } };
            co_await sendBuffers(bufs);
        }

        // 5) Request tags+commands+membership
        {
            static constexpr char cap[] =
                "CAP REQ :twitch.tv/membership "
                "twitch.tv/tags twitch.tv/commands\r\n";
            std::array<const_buffer, 1> bufs{ { buffer(cap) } };
            co_await sendBuffers(bufs);
        }

        // 6) JOIN each channel (zero-alloc)
        for (auto ch : channels) {
            std::array<const_buffer, 5> bufs{ {
                buffer("JOIN",4),
                buffer(" ",1),
                buffer("#",1),
                buffer(ch),
                buffer(CRLF_,2)
            } };
            co_await sendBuffers(bufs);
        }
    }


    boost::asio::awaitable<void> IrcClient::sendLine(
        std::string_view msg) noexcept
    {
        // build two-buffer array then delegate
        std::array<const_buffer, 2> bufs{ {
            buffer(msg),
            buffer(CRLF_,2)
        } };
        co_await sendBuffers(bufs);
    }


    boost::asio::awaitable<void> IrcClient::sendBuffers(
        std::span<const const_buffer> bufs) noexcept
    {
        try {
            ws_.text(true);
            co_await ws_.async_write(bufs, use_awaitable);
        }
        catch (...) {
            // swallow all errors, keep bot running
        }
    }


    boost::asio::awaitable<void> IrcClient::pingLoop() noexcept
    {
        for (;;) {
            ping_timer_.expires_after(std::chrono::minutes(4));
            co_await ping_timer_.async_wait(use_awaitable);
            co_await sendLine("PING :tmi.twitch.tv");
        }
    }


    void IrcClient::close() noexcept
    {
        // stop ping loop
        ping_timer_.cancel();

        // async-close WS + SSL on our strand
        auto ex = ws_.get_executor();
        boost::asio::post(ex, [this]() {
            // 1) WebSocket close
            ws_.async_close(close_code::normal,
                [](error_code) {});
            // 2) SSL shutdown
            ws_.next_layer().async_shutdown(
                [](error_code) {});
            });
    }

} // namespace twitch_bot
