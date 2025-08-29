#pragma once

// Twitch IRC client over WebSocket and TLS.
// Maps IRC lines to callbacks and provides convenience send helpers.
// Thread-safety: not thread safe. Call on the strand associated with ws_stream_.
// Docs: https://dev.twitch.tv/docs/chat/irc/

// C++ Standard Library
#include <array>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

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
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/beast/websocket/stream.hpp>

// Project
#include <tb/utils/attributes.hpp>

namespace twitch_bot
{

    /// Secure WebSocket IRC client for Twitch.
    /// Lifetime: the object must outlive any running coroutines started on it.
    /// Thread-safety: calls must be made on the ws_stream_ strand.
    class IrcClient
    {
    public:
        /// Construct a client bound to the given executor and SSL context.
        /// @param executor - Asio executor used for all async work.
        /// @param ssl_context - OpenSSL context already configured by the caller.
        /// @param access_token - Twitch OAuth token, prefixed with "oauth:".
        /// @param control_channel - Bot login name, also used as NICK.
        explicit IrcClient(boost::asio::any_io_executor executor,
                           boost::asio::ssl::context& ssl_context,
                           std::string_view access_token,
                           std::string_view control_channel);

        /// Destructor - best-effort wipe of the OAuth token.
        /// Does not implicitly close the socket. Ensure coroutines have finished or call close().
        ~IrcClient() noexcept;

        // Not copyable - owns a websocket stream and timer bound to an executor.
        IrcClient(const IrcClient&) = delete;
        IrcClient& operator=(const IrcClient&) = delete;

        // Movable - transfers stream and timer ownership. Marked noexcept for perf.
        IrcClient(IrcClient&&) noexcept = default;
        IrcClient& operator=(IrcClient&&) noexcept = default;

        /// Resolve, connect, perform TLS and WS handshakes, authenticate, and join channels.
        /// Preconditions: each channel name has no leading '#'.
        /// Throws on failure. On success, the connection is ready for reads and writes.
        [[nodiscard]] auto connect(std::span<const std::string_view> channels)
            -> boost::asio::awaitable<void>;

        /// Send a single IRC line. CRLF is appended automatically.
        /// No-throw. Best-effort: on failure, the connection is closed.
        [[nodiscard]] auto send_line(std::string_view message) noexcept -> boost::asio::awaitable<void>;

        /// Join or part a single channel. Channel name must not include '#'.
        /// No-throw. Best-effort: on failure, the connection is closed.
        [[nodiscard]] auto join(std::string_view channel) noexcept -> boost::asio::awaitable<void>;
        [[nodiscard]] auto part(std::string_view channel) noexcept -> boost::asio::awaitable<void>;

        /// Write pre-built buffers as one text frame.
        /// No-throw. Best-effort: on failure, the connection is closed.
        [[nodiscard]] auto send_buffers(std::span<const boost::asio::const_buffer> buffers) noexcept
            -> boost::asio::awaitable<void>;

        /// Send a PRIVMSG to a channel. Channel must not include '#'.
        /// No-throw. Best-effort.
        [[nodiscard]] auto privmsg(std::string_view channel, std::string_view text) noexcept
            -> boost::asio::awaitable<void>;

        /// Send a threaded reply using IRCv3 tags.
        /// If parent_msg_id is empty, falls back to PRIVMSG.
        /// No-throw. Best-effort.
        [[nodiscard]] auto reply(std::string_view channel,
                                 std::string_view parent_msg_id,
                                 std::string_view text) noexcept -> boost::asio::awaitable<void>;

        /// Send long text split into multiple PRIVMSGs.
        /// Behaviour: UTF-8 safe, up to 500 bytes per chunk. CR or LF are normalised to space.
        /// No-throw. Allocation free.
        [[nodiscard]] auto privmsg_wrap(std::string_view channel, std::string_view text) noexcept
            -> boost::asio::awaitable<void>;

        /// Send long text as threaded replies, all replying to the same parent.
        /// Behaviour: UTF-8 safe, 500 byte chunks. CR or LF are normalised to space.
        /// No-throw. Allocation free.
        [[nodiscard]] auto reply_wrap(std::string_view channel,
                                      std::string_view parent_msg_id,
                                      std::string_view text) noexcept -> boost::asio::awaitable<void>;

        /// Read frames, split on CRLF, and invoke handler for each complete line.
        /// Handler contract: void(std::string_view line)
        /// Ownership: the view points into internal buffers and is valid only during the call.
        /// Errors: may throw if the WS read fails or the connection closes with error.
        template<typename Handler>
        [[nodiscard]] auto read_loop(Handler handler) -> boost::asio::awaitable<void>;

        /// Issue PING every four minutes until cancelled or closed.
        /// Returns when the timer is cancelled or the connection is closed.
        [[nodiscard]] auto ping_loop() -> boost::asio::awaitable<void, boost::asio::any_io_executor>;

        /// Cancel timers and start a clean WebSocket close.
        /// No-throw. Idempotent.
        void close() noexcept;

        /// Replace the OAuth token. May allocate.
        void set_access_token(std::string_view token)
        {
            access_token_ = token;
        }

    private:
        static constexpr std::size_t k_read_buffer_size = 64ULL * 1024ULL; // bytes
        static constexpr std::string_view kCRLF{ "\r\n" };
        static constexpr std::size_t kMaxChatBytes = 500; // Twitch hard limit

        [[nodiscard]] static std::size_t utf8_clip_len(std::string_view s,
                                                       std::size_t max_bytes) noexcept;

        /// Choose a chunk end less than or equal to max_bytes, preferring the last ASCII space
        /// or line break within the window. Falls back to code point boundary.
        [[nodiscard]] static std::size_t
        utf8_chunk_by_words(std::string_view s, std::size_t start, std::size_t max_bytes) noexcept;

        using tcp_stream_type = boost::beast::tcp_stream;
        using ssl_stream_type = boost::asio::ssl::stream<tcp_stream_type>;
        using websocket_stream_type = boost::beast::websocket::stream<ssl_stream_type>;

        websocket_stream_type ws_stream_;
        boost::asio::steady_timer ping_timer_;
        boost::beast::flat_static_buffer<k_read_buffer_size> read_buffer_;

        // Carries a partial line that ended without CRLF in the previous read.
        std::string line_tail_;

        std::string access_token_;
        std::string control_channel_;

        boost::asio::steady_timer write_gate_;
        bool write_inflight_ = false;
    };

    template<typename Handler>
    [[nodiscard]] auto IrcClient::read_loop(Handler handler) -> boost::asio::awaitable<void>
    {
        static_assert(std::is_invocable_r_v<void, Handler, std::string_view>,
                      "Handler must be callable as void(std::string_view)");

        for (;;)
        {
            co_await ws_stream_.async_read(read_buffer_, boost::asio::use_awaitable);

            // Treat Beast DynamicBuffer as a sequence per the contract.
            auto const bs = read_buffer_.cdata();
            auto const total = boost::asio::buffer_size(bs);
            if (TB_UNLIKELY(total == 0))
            {
                continue;
            }

            auto const first = *boost::asio::buffer_sequence_begin(bs);
            auto const* ptr = static_cast<char const*>(first.data());
            std::string_view chunk{ ptr, total };

            if (line_tail_.empty())
            {
                // Zero-copy path. The handler must not retain the view.
                std::size_t begin = 0;
                for (;;)
                {
                    const auto r = chunk.find('\r', begin);
                    if (r == std::string_view::npos)
                    {
                        break;
                    }
                    if (TB_LIKELY(r + 1 < chunk.size() && chunk[r + 1] == '\n'))
                    {
                        std::string_view line{ chunk.data() + begin, r - begin };
                        if (!line.empty())
                        {
                            handler(line);
                        }
                        begin = r + 2;
                    }
                    else if (r + 1 == chunk.size())
                    {
                        // CR at end of chunk - carry the remainder to line_tail_
                        break;
                    }
                    else
                    {
                        // Isolated CR inside the chunk - treat as data and keep scanning
                        begin = r + 1;
                    }
                }
                if (begin < chunk.size())
                {
                    line_tail_.assign(chunk.data() + begin, chunk.size() - begin);
                }
            }
            else
            {
                // Accumulate into tail, then split on CRLF. Never emit a partial line.
                line_tail_.reserve(line_tail_.size() + chunk.size());
                line_tail_.append(chunk.data(), chunk.size());

                std::size_t begin = 0;
                for (;;)
                {
                    const auto r = line_tail_.find('\r', begin);
                    if (r == std::string::npos || r + 1 >= line_tail_.size())
                    {
                        break;
                    }
                    if (line_tail_[r + 1] == '\n')
                    {
                        std::string_view line{ line_tail_.data() + begin, r - begin };
                        if (!line.empty())
                        {
                            handler(line);
                        }
                        begin = r + 2;
                    }
                    else
                    {
                        begin = r + 1;
                    }
                }
                if (begin > 0)
                {
                    line_tail_.erase(0, begin);
                }
            }

            // Release exactly the bytes we just processed.
            read_buffer_.consume(total);
        }
    }

} // namespace twitch_bot
