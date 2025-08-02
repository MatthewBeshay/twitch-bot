#pragma once

// C++ Standard Library
#include <array>
#include <span>
#include <string>
#include <string_view>

// 3rd-party
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
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

/// Secure WebSocket IRC client.
class IrcClient
{
public:
    /// Create a client bound to \p executor using \p ssl_context and the supplied credentials.
    IrcClient(boost::asio::any_io_executor executor,
              boost::asio::ssl::context& ssl_context,
              std::string_view oauth_token,
              std::string_view nickname) noexcept;

    IrcClient(const IrcClient&) = delete;
    IrcClient(IrcClient&&) noexcept = default;
    IrcClient& operator=(IrcClient&&) = default;

    /// Connect, authenticate and join the given channels.
    auto connect(std::span<const std::string_view> channels) noexcept
        -> boost::asio::awaitable<void>;

    /// Send an IRC line, kCRLF added automatically.
    auto send_line(std::string_view message) noexcept -> boost::asio::awaitable<void>;

    /// Send pre-built buffers without allocating.
    auto send_buffers(std::span<const boost::asio::const_buffer> buffers) noexcept
        -> boost::asio::awaitable<void>;

    /// Read frames, split on kCRLF, call \p handler for each line.
    template <typename Handler>
    auto read_loop(Handler handler) noexcept -> boost::asio::awaitable<void>;

    /// Issue PING every four minutes until the stream closes.
    auto ping_loop() noexcept -> boost::asio::awaitable<void, boost::asio::any_io_executor>;

    /// Cancel timers and close the connection.
    void close() noexcept;

    void set_oauth_token(std::string_view tok) noexcept
    {
        oauth_token_ = tok;
    }

private:
    static constexpr std::size_t k_read_buffer_size = 8ULL * 1024ULL; ///< bytes

    static constexpr std::string_view kCRLF{"\r\n"}; ///< IRC line terminator

    using tcp_socket_type = boost::asio::ip::tcp::socket;
    using ssl_stream_type = boost::asio::ssl::stream<tcp_socket_type>;
    using websocket_stream_type = boost::beast::websocket::stream<ssl_stream_type>;

    websocket_stream_type ws_stream_;
    boost::asio::steady_timer ping_timer_;
    boost::beast::flat_static_buffer<k_read_buffer_size> read_buffer_;
    std::string oauth_token_;
    std::string nickname_;
};

template <typename Handler>
auto IrcClient::read_loop(Handler handler) noexcept -> boost::asio::awaitable<void>
{
    for (;;) {
        co_await ws_stream_.async_read(read_buffer_, boost::asio::use_awaitable);

        auto data = read_buffer_.data();
        std::string_view chunk{reinterpret_cast<const char*>(data.data()), data.size()};
        read_buffer_.consume(data.size());

        std::size_t pos = 0;
        while (pos < chunk.size()) {
            auto next = chunk.find(kCRLF, pos);
            std::string_view line{chunk.data() + pos,
                                  next == std::string_view::npos ? chunk.size() - pos : next - pos};
            pos = next == std::string_view::npos ? chunk.size() : next + kCRLF.size();

            if (!line.empty())
                handler(line);
        }
    }
}

} // namespace twitch_bot
