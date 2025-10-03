/*
Module Name:
- irc_client.hpp

Abstract:
- TLS WebSocket client for Twitch IRC.
- Designed for hot paths: avoids copies in read_loop and enforces UTF-8 safe write splitting.
- All calls must run on the ws_stream_ strand to prevent races. This keeps lifetime and ordering simple.

Why:
- Twitch limits messages to 500 bytes. We split on code point boundaries and prefer word edges to reduce spammy fragments.
- We keep a small line_tail_ to join frames that do not end with CRLF, so handlers only ever see complete lines.
- Best-effort send APIs trade strict erroring for resilience. On failure we close proactively to avoid half-dead sockets.
*/
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

// Core
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
        /// access_token must be "oauth:...". control_channel is also used as NICK.
        explicit IrcClient(boost::asio::any_io_executor executor,
                           boost::asio::ssl::context& ssl_context,
                           std::string_view access_token,
                           std::string_view control_channel);

        /// Destructor wipes the OAuth token best-effort.
        /// Rationale: avoid lingering secrets in memory longer than needed.
        ~IrcClient() noexcept;

        IrcClient(const IrcClient&) = delete;
        IrcClient& operator=(const IrcClient&) = delete;

        // Movable to transfer ownership of stream and timers without reallocating buffers.
        IrcClient(IrcClient&&) noexcept = default;
        IrcClient& operator=(IrcClient&&) noexcept = default;

        /// Resolve, connect, perform TLS and WS handshakes, authenticate, and join channels.
        /// Pre: channel names do not include '#'.
        [[nodiscard]] auto connect(std::span<const std::string_view> channels)
            -> boost::asio::awaitable<void>;

        /// Send one IRC line, CRLF appended internally.
        /// No-throw: on failure the connection is closed. Keeps caller code simple under failure.
        [[nodiscard]] auto send_line(std::string_view message) noexcept -> boost::asio::awaitable<void>;

        /// Join or part a single channel. Channel name must not include '#'.
        /// No-throw. Closes on failure to avoid a wedged session.
        [[nodiscard]] auto join(std::string_view channel) noexcept -> boost::asio::awaitable<void>;
        [[nodiscard]] auto part(std::string_view channel) noexcept -> boost::asio::awaitable<void>;

        /// Write pre-built buffers as a single text frame.
        /// No-throw. Closes on failure.
        [[nodiscard]] auto send_buffers(std::span<const boost::asio::const_buffer> buffers) noexcept
            -> boost::asio::awaitable<void>;

        /// Send a PRIVMSG to a channel. Channel must not include '#'.
        /// No-throw. Uses the same backpressure gate as other writes.
        [[nodiscard]] auto privmsg(std::string_view channel, std::string_view text) noexcept
            -> boost::asio::awaitable<void>;

        /// Send a threaded reply using IRCv3 tags. Falls back to PRIVMSG when parent_msg_id is empty.
        [[nodiscard]] auto reply(std::string_view channel,
                                 std::string_view parent_msg_id,
                                 std::string_view text) noexcept -> boost::asio::awaitable<void>;

        /// Long text split into multiple PRIVMSGs.
        /// Why: Twitch enforces a 500 byte cap. Splitting respects UTF-8 and tries word boundaries first.
        [[nodiscard]] auto privmsg_wrap(std::string_view channel, std::string_view text) noexcept
            -> boost::asio::awaitable<void>;

        /// Long text split into threaded replies to the same parent.
        [[nodiscard]] auto reply_wrap(std::string_view channel,
                                      std::string_view parent_msg_id,
                                      std::string_view text) noexcept -> boost::asio::awaitable<void>;

        /// Read frames, split on CRLF, and invoke handler for each complete line.
        /// Handler is given a view into internal buffers for zero-copy. Do not retain the view.
        /// Throws on read errors. Keeps lines whole so downstream parsers do not handle partials.
        template<typename Handler>
        [[nodiscard]] auto read_loop(Handler handler) -> boost::asio::awaitable<void>;

        /// Issue PING every four minutes until cancelled or closed.
        /// Why: keeps idle connections alive and detects stalls.
        [[nodiscard]] auto ping_loop() -> boost::asio::awaitable<void, boost::asio::any_io_executor>;

        /// Cancel timers and start a clean WebSocket close. Idempotent.
        void close() noexcept;

        /// Replace the OAuth token.
        void set_access_token(std::string_view token)
        {
            access_token_ = token;
        }

    private:
        static constexpr std::size_t k_read_buffer_size = 64ULL * 1024ULL; // small and cache friendly
        static constexpr std::string_view kCRLF{ "\r\n" };
        static constexpr std::size_t kMaxChatBytes = 500; // Twitch hard limit

        [[nodiscard]] static std::size_t utf8_clip_len(std::string_view s,
                                                       std::size_t max_bytes) noexcept;

        /// Choose a chunk end under max_bytes, prefer last ASCII space or line break.
        /// Falls back to code point boundary to avoid breaking UTF-8.
        [[nodiscard]] static std::size_t
        utf8_chunk_by_words(std::string_view s, std::size_t start, std::size_t max_bytes) noexcept;

        using tcp_stream_type = boost::beast::tcp_stream;
        using ssl_stream_type = boost::asio::ssl::stream<tcp_stream_type>;
        using websocket_stream_type = boost::beast::websocket::stream<ssl_stream_type>;

        websocket_stream_type ws_stream_;
        boost::asio::steady_timer ping_timer_;
        boost::beast::flat_static_buffer<k_read_buffer_size> read_buffer_;

        // Carries a partial line between reads so handlers only see complete lines.
        std::string line_tail_;

        std::string access_token_;
        std::string control_channel_;

        // Serialise writes to avoid interleaving frames from multiple coroutines.
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

            // Use the DynamicBuffer as a single contiguous view when possible.
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
                // Zero-copy path: emit lines directly from the current buffer slice.
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
                        // CR at end - save for the next frame so we only emit complete lines.
                        break;
                    }
                    else
                    {
                        // Isolated CR - treat as data.
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
                // Join with carry-over so handlers never see partial lines.
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

            // Consume exactly what we inspected so the buffer does not grow unbounded.
            read_buffer_.consume(total);
        }
    }

} // namespace twitch_bot
