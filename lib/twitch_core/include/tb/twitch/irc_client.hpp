#pragma once

// C++ Standard Library
#include <array>
#include <span>
#include <string>
#include <string_view>

// Boost.Asio
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>

// Boost.Beast
#include <boost/beast/core/flat_buffer.hpp>
#include <boost/beast/core/static_buffer.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/beast/websocket/stream.hpp>

// Project
#include <tb/utils/attributes.hpp>

namespace twitch_bot {

// Secure WebSocket IRC client.
class IrcClient
{
public:
    // Create a client bound to \p executor using \p ssl_context and the supplied credentials.
    IrcClient(boost::asio::any_io_executor executor,
              boost::asio::ssl::context& ssl_context,
              std::string_view access_token,
              std::string_view control_channel);

    IrcClient(const IrcClient&) = delete;
    IrcClient(IrcClient&&) noexcept = default;
    IrcClient& operator=(IrcClient&&) = default;

    // Connect, authenticate and join the given channels.
    auto connect(std::span<const std::string_view> channels) -> boost::asio::awaitable<void>;

    // Send an IRC line, CRLF added automatically.
    auto send_line(std::string_view message) noexcept -> boost::asio::awaitable<void>;

    // Send pre-built buffers without allocating.
    auto send_buffers(std::span<const boost::asio::const_buffer> buffers) noexcept
        -> boost::asio::awaitable<void>;

    // Convenience: send a PRIVMSG to channel. Channel must not include '#'.
    auto privmsg(std::string_view channel, std::string_view text) noexcept
        -> boost::asio::awaitable<void>;

    // Convenience: send a threaded reply using IRCv3 tags.
    // Channel must not include '#'. parent_msg_id should come from the incoming message @id tag.
    auto reply(std::string_view channel,
               std::string_view parent_msg_id,
               std::string_view text) noexcept -> boost::asio::awaitable<void>;

    // Wrap long text into multiple PRIVMSGs (UTF-8 safe, 500-byte chunks).
    auto privmsg_wrap(std::string_view channel, std::string_view text) noexcept
        -> boost::asio::awaitable<void>;

    // Wrap long text into multiple threaded replies (all reply to the same parent).
    auto reply_wrap(std::string_view channel,
                    std::string_view parent_msg_id,
                    std::string_view text) noexcept -> boost::asio::awaitable<void>;

    // Read frames, split on CRLF, call \p handler for each line (never emits partial lines).
    template <typename Handler> auto read_loop(Handler handler) -> boost::asio::awaitable<void>;

    // Issue PING every four minutes until the stream closes.
    auto ping_loop() -> boost::asio::awaitable<void, boost::asio::any_io_executor>;

    // Cancel timers and close the connection.
    void close() noexcept;

    void set_access_token(std::string_view tok) noexcept
    {
        access_token_ = tok;
    }

private:
    static constexpr std::size_t k_read_buffer_size = 8ULL * 1024ULL; //< bytes
    static constexpr std::string_view kCRLF{"\r\n"};
    static constexpr std::size_t kMaxChatBytes = 500; //< Twitch hard limit

    static void sanitize_crlf(std::string& s) noexcept;

    [[nodiscard]] static std::size_t utf8_clip_len(std::string_view s,
                                                   std::size_t max_bytes) noexcept;

    // choose a chunk end ≤ max that prefers the last ASCII space if possible
    [[nodiscard]] static std::size_t
    utf8_chunk_by_words(std::string_view s, std::size_t start, std::size_t max_bytes) noexcept;

    using tcp_socket_type = boost::asio::ip::tcp::socket;
    using ssl_stream_type = boost::asio::ssl::stream<tcp_socket_type>;
    using websocket_stream_type = boost::beast::websocket::stream<ssl_stream_type>;

    websocket_stream_type ws_stream_;
    boost::asio::steady_timer ping_timer_;
    boost::beast::flat_static_buffer<k_read_buffer_size> read_buffer_;

    // Carries over a partial line that ended without CRLF in the previous read
    std::string line_tail_;

    std::string access_token_;
    std::string control_channel_;
};

template <typename Handler>
auto IrcClient::read_loop(Handler handler) -> boost::asio::awaitable<void>
{
    for (;;) {
        co_await ws_stream_.async_read(read_buffer_, boost::asio::use_awaitable);

        const auto data = read_buffer_.data();
        std::string_view chunk{reinterpret_cast<const char*>(data.data()), data.size()};
        read_buffer_.consume(data.size());

        // Accumulate into tail, then split on CRLF — never emit a partial line
        line_tail_.append(chunk.data(), chunk.size());

        std::size_t begin = 0;
        for (;;) {
            const auto pos = line_tail_.find(kCRLF, begin);
            if (pos == std::string::npos)
                break;

            std::string_view line{line_tail_.data() + begin, pos - begin};
            if (!line.empty())
                handler(line);

            begin = pos + kCRLF.size();
        }

        // Keep the remaining partial tail (if any)
        if (begin > 0) {
            line_tail_.erase(0, begin);
        }
    }
}

} // namespace twitch_bot
