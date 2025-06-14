#pragma once

// C++ Standard Library
#include <array>
#include <span>
#include <string>
#include <string_view>

// 3rd-party
#include <boost/asio/awaitable.hpp>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/static_buffer.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/beast/websocket/stream.hpp>

// Project
#include "utils/attributes.hpp"

namespace twitch_bot {

/// IRC client over secure WebSocket.
class IrcClient
{
public:
    /// Prepare client with executor, TLS context and credentials.
    IrcClient(boost::asio::any_io_executor executor,
              boost::asio::ssl::context&     ssl_context,
              std::string_view               oauth_token,
              std::string_view               nickname) noexcept;

    ~IrcClient() = default;

    IrcClient(const IrcClient&) = delete;
    IrcClient& operator=(const IrcClient&) = delete;

    /// Open connection, authenticate and join channels.
    boost::asio::awaitable<void>
    connect(std::span<const std::string_view> channels) noexcept;

    /// Append CRLF and send an IRC message.
    boost::asio::awaitable<void>
    send_line(std::string_view message) noexcept;

    /// Send raw buffers without heap allocation.
    boost::asio::awaitable<void>
    send_buffers(std::span<const boost::asio::const_buffer> buffers) noexcept;

    /// Read frames, split on CRLF, and invoke handler for each line.
    template <typename Handler>
    boost::asio::awaitable<void>
    read_loop(Handler&& handler) noexcept;

    /// Periodic PING every four minutes until closed.
    boost::asio::awaitable<void, boost::asio::any_io_executor>
    ping_loop() noexcept;

    /// Stop ping timer and close connection gracefully.
    void close() noexcept;

private:
    static constexpr std::size_t     k_read_buffer_size = 8 * 1024;
    static constexpr std::string_view k_crlf            = "\r\n";

    using tcp_socket_type       = boost::asio::ip::tcp::socket;
    using ssl_stream_type       = boost::asio::ssl::stream<tcp_socket_type>;
    using websocket_stream_type = boost::beast::websocket::stream<ssl_stream_type>;

    websocket_stream_type                   ws_stream_;
    boost::asio::steady_timer               ping_timer_;
    boost::beast::flat_static_buffer<k_read_buffer_size> read_buffer_;
    std::string                             oauth_token_;
    std::string                             nickname_;
};

template <typename Handler>
boost::asio::awaitable<void>
IrcClient::read_loop(Handler&& handler) noexcept
{
    for (;;)
    {
        co_await ws_stream_.async_read(read_buffer_,
                                       boost::asio::use_awaitable);

        auto data = read_buffer_.data();
        std::string_view chunk{
            reinterpret_cast<const char*>(data.data()),
            data.size()
        };
        read_buffer_.consume(data.size());

        std::size_t pos = 0;
        while (pos < chunk.size())
        {
            auto next = chunk.find(k_crlf, pos);
            std::string_view line{
                chunk.data() + pos,
                next == std::string_view::npos
                  ? chunk.size() - pos
                  : next - pos
            };
            pos = next == std::string_view::npos
                  ? chunk.size()
                  : next + k_crlf.size();

            if (!line.empty())
                handler(line);
        }
    }
}

} // namespace twitch_bot