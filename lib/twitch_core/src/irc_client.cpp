// Twitch IRC client implementation.
// Uses Beast tcp_stream with TLS and WebSocket.

// C++ Standard Library
#include <algorithm>
#include <cassert>
#include <chrono>
#include <string>

// Boost.Asio
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/socket_base.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

// Boost.Beast
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

// Boost.System
#include <boost/system/error_code.hpp>

// OpenSSL
#include <openssl/ssl.h>

// Project
#include <tb/twitch/irc_client.hpp>

namespace twitch_bot
{

    using boost::asio::buffer;
    using boost::asio::const_buffer;
    using boost::asio::use_awaitable;
    using error_code = boost::system::error_code;
    namespace beast = boost::beast;

    IrcClient::IrcClient(boost::asio::any_io_executor executor,
                         boost::asio::ssl::context& ssl_context,
                         std::string_view access_token,
                         std::string_view control_channel) :
        ws_stream_{ boost::asio::make_strand(executor), ssl_context }, ping_timer_{ executor }, access_token_{ access_token }, control_channel_{ control_channel }, write_gate_{ executor }
    {
        write_gate_.expires_at(std::chrono::steady_clock::time_point::max());
    }

    IrcClient::~IrcClient() noexcept
    {
        // Best-effort wipe. Compilers may optimise this, but it helps in practice.
        std::fill(access_token_.begin(), access_token_.end(), '\0');
    }

    auto IrcClient::connect(std::span<const std::string_view> channels) -> boost::asio::awaitable<void>
    {
        static const char host_name[] = "irc-ws.chat.twitch.tv";
        static const char port_str[] = "443";

        auto executor = co_await boost::asio::this_coro::executor;

        // DNS and TCP connect with deadline control.
        boost::asio::ip::tcp::resolver resolver{ executor };
        auto results = co_await resolver.async_resolve(host_name, port_str, use_awaitable);

        auto& tcp = beast::get_lowest_layer(ws_stream_);
        tcp.expires_after(std::chrono::seconds(30));
        co_await tcp.async_connect(results, use_awaitable);
        tcp.expires_never();

        // Low latency socket options.
        tcp.socket().set_option(boost::asio::ip::tcp::no_delay(true));
        tcp.socket().set_option(boost::asio::socket_base::keep_alive(true));

        // TLS setup and verification policy.
        auto& ssl = ws_stream_.next_layer();

        if (SSL* s = ssl.native_handle())
        {
            if (SSL_CTX* ctx = ::SSL_get_SSL_CTX(s))
            {
                if (::SSL_CTX_load_verify_locations(ctx, TB_CACERT_PEM_PATH, nullptr) != 1)
                {
                    const unsigned long e = ::ERR_get_error();
                    throw std::system_error{ static_cast<int>(e), boost::asio::error::get_ssl_category(), "failed to load CA bundle" };
                }
            }
            else
            {
                throw std::runtime_error("SSL context unavailable");
            }
        }
        else
        {
            throw std::runtime_error("SSL handle unavailable");
        }

        // SNI and hostname verification.
        if (!::SSL_set_tlsext_host_name(ssl.native_handle(), host_name))
        {
            throw std::system_error{ static_cast<int>(::ERR_get_error()),
                                     boost::asio::error::get_ssl_category(),
                                     "SNI failure" };
        }
        (void)::SSL_set1_host(ssl.native_handle(), host_name);
        ssl.set_verify_mode(boost::asio::ssl::verify_peer);

        // TLS handshake with a deadline.
        tcp.expires_after(std::chrono::seconds(30));
        co_await ssl.async_handshake(boost::asio::ssl::stream_base::client, use_awaitable);
        tcp.expires_never();

        // WebSocket configuration.
        ws_stream_.set_option(
            beast::websocket::stream_base::timeout::suggested(beast::role_type::client));
        ws_stream_.set_option(
            beast::websocket::stream_base::decorator([](beast::websocket::request_type& req) {
                req.set(beast::http::field::origin, "https://www.twitch.tv");
                req.set(beast::http::field::user_agent, "TwitchBotCore/1.0 (+irc)");
            }));
        ws_stream_.auto_fragment(false);
        ws_stream_.read_message_max(k_read_buffer_size);

        // WebSocket handshake.
        tcp.expires_after(std::chrono::seconds(30));
        co_await ws_stream_.async_handshake(host_name, "/", use_awaitable);
        tcp.expires_never();

        // IRC writes are text frames.
        ws_stream_.text(true);

        // Protocol tokens - string_view avoids mismatched lengths.
        using sv = std::string_view;
        static constexpr sv PASS_ = "PASS ";
        static constexpr sv NICK_ = "NICK ";
        static constexpr sv CAPS = "CAP REQ :twitch.tv/membership twitch.tv/tags twitch.tv/commands\r\n";

        // PASS, NICK, CAP.
        {
            std::array<const_buffer, 3> bufs_pass{ buffer(PASS_), buffer(access_token_), boost::asio::buffer(kCRLF) };
            co_await send_buffers(bufs_pass);
        }
        {
            std::array<const_buffer, 3> bufs_nick{ buffer(NICK_), buffer(control_channel_), boost::asio::buffer(kCRLF) };
            co_await send_buffers(bufs_nick);
        }
        {
            std::array<const_buffer, 1> bufs{ buffer(CAPS) };
            co_await send_buffers(bufs);
        }

#ifndef NDEBUG
        // Channels must not include '#'.
        for (auto ch : channels)
        {
            assert(ch.find('#') == std::string_view::npos);
        }
#endif

        // JOIN multiple channels, respecting the 512 byte IRC limit.
        // Format: "JOIN #a,#b,#c\r\n"
        if (!channels.empty())
        {
            static constexpr std::size_t k_irc_max_line = 512; // includes CRLF
            std::size_t total_names = 0;
            for (auto ch : channels)
            {
                total_names += ch.size();
            }
            std::string line;
            line.reserve(6 + 2 * channels.size() + total_names + 2); // "JOIN " + '#' and commas + CRLF

            line.assign("JOIN ");

            bool first = true;
            for (auto ch : channels)
            {
                const std::size_t needed = (first ? 0 : 1) + 1 + ch.size(); // comma + '#' + name
                if (line.size() + needed + 2 > k_irc_max_line)
                {
                    line.append("\r\n");
                    std::array<const_buffer, 1> bufs{ buffer(line) };
                    co_await send_buffers(bufs);
                    line.assign("JOIN ");
                    first = true;
                }

                if (!first)
                {
                    line.push_back(',');
                }
                line.push_back('#');
                line.append(ch.data(), ch.size());
                first = false;
            }

            if (line.size() > 5)
            { // more than "JOIN "
                line.append("\r\n");
                std::array<const_buffer, 1> bufs{ buffer(line) };
                co_await send_buffers(bufs);
            }
        }
    }

    auto IrcClient::send_line(std::string_view message) noexcept -> boost::asio::awaitable<void>
    {
        std::array<const_buffer, 2> bufs{ buffer(message), boost::asio::buffer(kCRLF) };
        co_await send_buffers(bufs);
    }

    auto IrcClient::join(std::string_view channel) noexcept -> boost::asio::awaitable<void>
    {
        assert(channel.find('#') == std::string_view::npos);

        static constexpr std::string_view JOIN_HASH = "JOIN #";
        std::array<const_buffer, 3> bufs{ buffer(JOIN_HASH), buffer(channel), boost::asio::buffer(kCRLF) };
        co_await send_buffers(bufs);
    }

    auto IrcClient::part(std::string_view channel) noexcept -> boost::asio::awaitable<void>
    {
        assert(channel.find('#') == std::string_view::npos);

        static constexpr std::string_view PART_HASH = "PART #";
        std::array<const_buffer, 3> bufs{ buffer(PART_HASH), buffer(channel), boost::asio::buffer(kCRLF) };
        co_await send_buffers(bufs);
    }

    auto IrcClient::send_buffers(std::span<const boost::asio::const_buffer> buffers) noexcept
        -> boost::asio::awaitable<void>
    {
        try
        {
            while (write_inflight_)
            {
                boost::system::error_code ec;
                // Make sure the timer is in a waitable state.
                write_gate_.expires_at(std::chrono::steady_clock::time_point::max());
                co_await write_gate_.async_wait(
                    boost::asio::redirect_error(boost::asio::use_awaitable, ec));
            }

            write_inflight_ = true;
            co_await ws_stream_.async_write(buffers, boost::asio::use_awaitable);
            write_inflight_ = false;

            write_gate_.cancel();
        }
        catch (...)
        {
            write_inflight_ = false;
            try
            {
                write_gate_.cancel();
            }
            catch (...)
            {
            }

            try
            {
                close();
            }
            catch (...)
            {
            }
            co_return;
        }
    }

    auto IrcClient::privmsg(std::string_view channel, std::string_view text) noexcept
        -> boost::asio::awaitable<void>
    {
        assert(channel.find('#') == std::string_view::npos);

        static constexpr std::string_view PRIVMSG_HASH = "PRIVMSG #";
        static constexpr std::string_view SPACE_COLON = " :";
        std::array<const_buffer, 5> bufs{ buffer(PRIVMSG_HASH), buffer(channel), buffer(SPACE_COLON), buffer(text), boost::asio::buffer(kCRLF) };
        co_await send_buffers(bufs);
    }

    auto IrcClient::reply(std::string_view channel,
                          std::string_view parent_msg_id,
                          std::string_view text) noexcept -> boost::asio::awaitable<void>
    {
        assert(channel.find('#') == std::string_view::npos);

        if (parent_msg_id.empty())
        {
            co_await privmsg(channel, text);
            co_return;
        }

        static constexpr std::string_view REPLY_TAG = "@reply-parent-msg-id=";
        static constexpr std::string_view SPACE_PRIV = " PRIVMSG #";
        static constexpr std::string_view SPACE_COLON = " :";

        std::array<const_buffer, 7> bufs{
            buffer(REPLY_TAG), buffer(parent_msg_id), buffer(SPACE_PRIV), buffer(channel), buffer(SPACE_COLON), buffer(text), boost::asio::buffer(kCRLF)
        };
        co_await send_buffers(bufs);
    }

    // --------- helpers for wrapped sending ---------

    std::size_t IrcClient::utf8_clip_len(std::string_view s, std::size_t max_bytes) noexcept
    {
        if (s.size() <= max_bytes)
        {
            return s.size();
        }
        std::size_t i = max_bytes;
        while (i > 0 && (static_cast<unsigned char>(s[i - 1]) & 0xC0) == 0x80)
        {
            --i;
        }
        return i;
    }

    std::size_t IrcClient::utf8_chunk_by_words(std::string_view s,
                                               std::size_t start,
                                               std::size_t max_bytes) noexcept
    {
        if (start >= s.size())
        {
            return 0;
        }

        const std::size_t remaining = s.size() - start;
        const std::size_t hard = utf8_clip_len(s.substr(start), std::min(max_bytes, remaining));
        if (hard == 0)
        {
            return 0;
        }

        std::size_t end = start + hard;

        for (std::size_t i = end; i > start; --i)
        {
            const char c = s[i - 1];
            if (c == ' ' || c == '\r' || c == '\n')
            {
                end = i - 1;
                break;
            }
        }
        if (end == start)
        {
            end = start + hard;
        }
        return end - start;
    }

    // Allocation free wrappers - remain noexcept and keep latency stable.

    auto IrcClient::privmsg_wrap(std::string_view channel, std::string_view text) noexcept
        -> boost::asio::awaitable<void>
    {
        assert(channel.find('#') == std::string_view::npos);

        auto has_crlf = [](std::string_view v) { return v.find_first_of("\r\n") != std::string_view::npos; };

        if (text.size() <= kMaxChatBytes && !has_crlf(text))
        {
            co_await privmsg(channel, text);
            co_return;
        }

        static constexpr std::string_view PRIVMSG_HASH = "PRIVMSG #";
        static constexpr std::string_view SPACE_COLON = " :";

        std::array<char, kMaxChatBytes> out{};

        std::size_t pos = 0;
        while (pos < text.size())
        {
            const std::size_t len = utf8_chunk_by_words(text, pos, kMaxChatBytes);
            if (len == 0)
            {
                break;
            }

            for (std::size_t i = 0; i < len; ++i)
            {
                char c = text[pos + i];
                out[i] = (c == '\r' || c == '\n') ? ' ' : c;
            }
            std::string_view cleaned{ out.data(), len };

            std::array<const_buffer, 5> bufs{ buffer(PRIVMSG_HASH), buffer(channel), buffer(SPACE_COLON), buffer(cleaned), boost::asio::buffer(kCRLF) };
            co_await send_buffers(bufs);

            pos += len;
            while (pos < text.size())
            {
                const char c = text[pos];
                if (c == ' ' || c == '\r' || c == '\n')
                {
                    ++pos;
                }
                else
                {
                    break;
                }
            }
        }
    }

    auto IrcClient::reply_wrap(std::string_view channel,
                               std::string_view parent_msg_id,
                               std::string_view text) noexcept -> boost::asio::awaitable<void>
    {
        assert(channel.find('#') == std::string_view::npos);

        if (parent_msg_id.empty())
        {
            co_await privmsg_wrap(channel, text);
            co_return;
        }

        auto has_crlf = [](std::string_view v) { return v.find_first_of("\r\n") != std::string_view::npos; };

        if (text.size() <= kMaxChatBytes && !has_crlf(text))
        {
            co_await reply(channel, parent_msg_id, text);
            co_return;
        }

        static constexpr std::string_view REPLY_TAG = "@reply-parent-msg-id=";
        static constexpr std::string_view SPACE_PRIV = " PRIVMSG #";
        static constexpr std::string_view SPACE_COLON = " :";

        std::array<char, kMaxChatBytes> out{};

        std::size_t pos = 0;
        while (pos < text.size())
        {
            const std::size_t len = utf8_chunk_by_words(text, pos, kMaxChatBytes);
            if (len == 0)
            {
                break;
            }

            for (std::size_t i = 0; i < len; ++i)
            {
                char c = text[pos + i];
                out[i] = (c == '\r' || c == '\n') ? ' ' : c;
            }
            std::string_view cleaned{ out.data(), len };

            std::array<const_buffer, 7> bufs{
                buffer(REPLY_TAG), buffer(parent_msg_id), buffer(SPACE_PRIV), buffer(channel), buffer(SPACE_COLON), buffer(cleaned), boost::asio::buffer(kCRLF)
            };
            co_await send_buffers(bufs);

            pos += len;
            while (pos < text.size())
            {
                const char c = text[pos];
                if (c == ' ' || c == '\r' || c == '\n')
                {
                    ++pos;
                }
                else
                {
                    break;
                }
            }
        }
    }

    auto IrcClient::ping_loop() -> boost::asio::awaitable<void, boost::asio::any_io_executor>
    {
        for (;;)
        {
            ping_timer_.expires_after(std::chrono::minutes{ 4 });

            // Cancellation surfaces as an error_code.
            boost::system::error_code ec;
            co_await ping_timer_.async_wait(
                boost::asio::redirect_error(boost::asio::use_awaitable, ec));

            if (ec)
            {
                co_return;
            }

            co_await send_line("PING :tmi.twitch.tv");
        }
    }

    void IrcClient::close() noexcept
    {
        ping_timer_.cancel();

        // Best-effort clean close. Do not call async_shutdown on TLS here.
        try
        {
            boost::asio::dispatch(ws_stream_.get_executor(), [this] {
                ws_stream_.async_close(beast::websocket::close_code::normal,
                                       [](error_code) { /* ignore */ });
            });
        }
        catch (...)
        {
            // Ignore dispatch failures.
        }
    }

} // namespace twitch_bot
