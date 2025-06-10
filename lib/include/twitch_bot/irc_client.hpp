#pragma once

#include <array>
#include <span>
#include <string>
#include <string_view>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/buffer.hpp>

#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/beast/websocket/stream.hpp>

namespace twitch_bot {

/// IRC client over WSS. All methods are async and non-blocking.
class IrcClient {
public:
    /// Construct with executor, TLS context, OAuth token and nick.
    IrcClient(boost::asio::any_io_executor exec,
              boost::asio::ssl::context&    ssl_ctx,
              std::string_view              oauth_token,
              std::string_view              nickname) noexcept;

    ~IrcClient() = default;
    IrcClient(const IrcClient&) = delete;
    IrcClient& operator=(const IrcClient&) = delete;

    /// Resolve, handshake TCP→SSL→WS, authenticate and JOIN channels.
    /// @channels Span of channel names (no leading '#').
    boost::asio::awaitable<void>
    connect(std::span<const std::string_view> channels) noexcept;

    /// Send one IRC line (msg + CRLF) with zero heap alloc.
    boost::asio::awaitable<void>
    sendLine(std::string_view msg) noexcept;

    /// Send raw buffers (zero alloc) on the WebSocket.
    boost::asio::awaitable<void>
    sendBuffers(std::span<const boost::asio::const_buffer> bufs) noexcept;

    /// Read frames, split on CRLF, invoke callback per line.
    template<class F>
    boost::asio::awaitable<void>
    readLoop(F&& callback) noexcept;

    /// Send a PING every 4 minutes until close() is called.
    boost::asio::awaitable<void>
    pingLoop() noexcept;

    /// Cancel ping timer and initiate non-blocking WS+SSL close.
    void close() noexcept;

private:
    using tcp_socket_t = boost::asio::ip::tcp::socket;
    using ssl_stream_t = boost::asio::ssl::stream<tcp_socket_t>;
    using ws_stream_t  = boost::beast::websocket::stream<ssl_stream_t>;

    ws_stream_t               ws_;           // WebSocket-over-SSL stream
    boost::asio::steady_timer ping_timer_;
    boost::beast::flat_buffer read_buffer_;

    std::string oauth_token_;
    std::string nickname_;

    static inline constexpr char CRLF_[3] = "\r\n";
};

template<class F>
boost::asio::awaitable<void>
IrcClient::readLoop(F&& callback) noexcept
{
    for (;;) {
        co_await ws_.async_read(read_buffer_, boost::asio::use_awaitable);

        auto data = read_buffer_.data();
        std::string_view chunk{
            reinterpret_cast<const char*>(data.data()),
            data.size()
        };
        read_buffer_.consume(data.size());

        std::size_t pos = 0;
        while (pos < chunk.size()) {
            auto next = chunk.find(CRLF_, pos);
            std::string_view line = chunk.substr(
                pos,
                next == std::string_view::npos
                  ? chunk.size() - pos
                  : next - pos
            );
            pos = (next == std::string_view::npos)
                ? chunk.size()
                : next + 2;
            if (!line.empty()) {
                callback(line);
            }
        }
    }
}

} // namespace twitch_bot
