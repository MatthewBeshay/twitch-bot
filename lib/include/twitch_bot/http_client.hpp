#pragma once

// C++ standard library
#include <chrono>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// 3rd-party
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_allocator.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>

#include <openssl/ssl.h>

#include <glaze/json.hpp>

// Project
#include "utils/attributes.hpp"
#include "utils/chunked_encoding.hpp"
#include "utils/transparent_string.hpp"

namespace http_client {
namespace detail {

    // Build a key for the connection-pool map.
    inline std::string make_pool_key(std::string_view host, std::string_view port) noexcept
    {
        std::string key;
        key.reserve(host.size() + 1 + port.size());
        key.append(host);
        key.push_back(':');
        key.append(port);
        return key;
    }

    // Build an error message with host, target and status code.
    inline std::string
    make_error_msg(std::string_view host, std::string_view target, int status) noexcept
    {
        std::string msg;
        msg.reserve(host.size() + target.size() + 32);
        msg.append(host).append(target).append(" returned ").append(std::to_string(status));
        return msg;
    }

} // namespace detail

// JSON options visible to both header and source
inline constexpr glz::opts json_opts{.null_terminated = true,
                                     .error_on_unknown_keys = false,
                                     .minified = true};

// Type aliases for JSON values and results
using json = glz::json_t;
using result = glz::expected<json, glz::error_ctx>;

// HTTP header types
using http_header = std::pair<std::string_view, std::string_view>;
using http_headers = std::span<const http_header>;

// Default pool sizes
inline constexpr std::size_t k_default_expected_hosts = 16;
inline constexpr std::size_t k_default_connections_per_host = 4;

// HTTP and pooling constants
inline constexpr int k_http_version = 11;
inline constexpr auto k_tcp_connect_timeout = std::chrono::seconds{30};
inline constexpr auto k_handshake_timeout = std::chrono::seconds{10};
inline constexpr auto k_http_write_timeout = std::chrono::seconds{10};
inline constexpr auto k_http_read_timeout = std::chrono::seconds{10};
inline constexpr auto k_pool_idle_timeout = std::chrono::seconds{60};
inline constexpr std::size_t k_buffer_size_kb = 16;

class client
{
public:
    using allocator_type = std::pmr::polymorphic_allocator<std::byte>;

    client(boost::asio::any_io_executor executor,
           boost::asio::ssl::context &ssl_context,
           std::size_t expected_hosts = k_default_expected_hosts,
           std::size_t expected_conns_per_host = k_default_connections_per_host) noexcept;

    allocator_type get_allocator() const noexcept;

    [[nodiscard]]
    boost::asio::awaitable<result> get(std::string_view host,
                                       std::string_view port,
                                       std::string_view target,
                                       http_headers headers = {}) noexcept;

    [[nodiscard]]
    boost::asio::awaitable<result> post(std::string_view host,
                                        std::string_view port,
                                        std::string_view target,
                                        std::string_view body,
                                        http_headers headers = {}) noexcept;

    /// Decode a chunked GET stream. Handler is called for each chunk.
    template <typename ChunkHandler>
    boost::asio::awaitable<void> stream_get(std::string_view host,
                                            std::string_view port,
                                            std::string_view target,
                                            http_headers headers,
                                            ChunkHandler handler) noexcept;

private:
    struct connection {
        using tcp_stream = boost::beast::tcp_stream;
        using ssl_stream = boost::beast::ssl_stream<tcp_stream>;

        ssl_stream stream;
        static constexpr std::size_t buffer_size = k_buffer_size_kb * 1024U;
        boost::beast::flat_static_buffer<buffer_size> buffer;
        std::chrono::steady_clock::time_point last_used;

        explicit connection(ssl_stream s) noexcept
            : stream(std::move(s)), buffer(), last_used(std::chrono::steady_clock::now())
        {
        }

        void reset() noexcept
        {
            buffer.clear();
        }

        void mark_used() noexcept
        {
            last_used = std::chrono::steady_clock::now();
        }
    };

    [[nodiscard]]
    boost::asio::awaitable<result> perform(boost::beast::http::verb method,
                                           std::string_view host,
                                           std::string_view port,
                                           std::string_view target,
                                           std::string_view body,
                                           http_headers headers) noexcept;

    boost::asio::any_io_executor executor_;
    boost::asio::ssl::context *ssl_context_;
    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::strand<boost::asio::any_io_executor> strand_;

    mutable std::pmr::monotonic_buffer_resource handler_buffer_;
    std::unordered_map<std::string,
                       std::pmr::vector<std::shared_ptr<connection>>,
                       TransparentStringHash,
                       TransparentStringEq>
        pool_;

    std::size_t expected_conns_per_host_;
};

// stream_get template implementation
template <typename ChunkHandler>
boost::asio::awaitable<void> client::stream_get(std::string_view host,
                                                std::string_view port,
                                                std::string_view target,
                                                http_headers headers,
                                                ChunkHandler handler) noexcept
{
    // Bind executor and allocator to all operations
    auto tok = boost::asio::bind_allocator(
        get_allocator(), boost::asio::bind_executor(strand_, boost::asio::use_awaitable));

    // Build pool key and obtain or create connection vector
    std::string key = detail::make_pool_key(host, port);
    auto [it, inserted]
        = pool_.try_emplace(key, std::pmr::vector<std::shared_ptr<connection>>(&handler_buffer_));
    auto &vec = it->second;
    if (inserted) {
        vec.reserve(expected_conns_per_host_);
    }

    std::shared_ptr<connection> conn;
    if (!vec.empty()) {
        conn = std::move(vec.back());
        vec.pop_back();
    }

    // Drop idle connections
    if (conn && std::chrono::steady_clock::now() - conn->last_used > k_pool_idle_timeout) {
        conn.reset();
    }

    if (!conn) {
        // Resolve DNS
        auto endpoints = co_await resolver_.async_resolve(host, port, tok);

        // Establish TCP connection
        boost::beast::tcp_stream tcp(executor_);
        tcp.expires_after(k_tcp_connect_timeout);
        co_await tcp.async_connect(endpoints, tok);

        // Disable Nagle's algorithm
        tcp.socket().set_option(boost::asio::ip::tcp::no_delay{true});

        // Upgrade to TLS
        boost::beast::ssl_stream<boost::beast::tcp_stream> ssl{std::move(tcp), *ssl_context_};
        if (!SSL_set_tlsext_host_name(ssl.native_handle(), host.data())) {
            throw std::system_error{static_cast<int>(::ERR_get_error()),
                                    boost::asio::error::get_ssl_category(), "SNI failure"};
        }
        ssl.next_layer().expires_after(k_handshake_timeout);
        co_await ssl.async_handshake(boost::asio::ssl::stream_base::client, tok);

        conn = std::make_shared<connection>(std::move(ssl));
    }

    // Send GET request
    conn->reset();
    boost::beast::http::request<boost::beast::http::empty_body> req{boost::beast::http::verb::get,
                                                                    target, k_http_version};
    req.set(boost::beast::http::field::host, host);
    req.set(boost::beast::http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    for (auto &h : headers) {
        req.set(h.first, h.second);
    }
    conn->stream.next_layer().expires_after(k_http_write_timeout);
    co_await boost::beast::http::async_write(conn->stream, req, tok);

    // Read response headers
    boost::beast::http::response_parser<boost::beast::http::empty_body> parser;
    co_await boost::beast::http::async_read_header(conn->stream, conn->buffer, parser, tok);
    auto &resp = parser.get();
    bool is_chunked = resp.base()["transfer-encoding"] == "chunked";

    if (is_chunked) {
        uint64_t chunk_state = 0;
        std::string_view raw_buf;

        while (true) {
            // Set read timeout
            conn->stream.next_layer().expires_after(k_http_read_timeout);

            // Read decrypted data
            std::size_t n = co_await conn->stream.async_read_some(
                boost::asio::buffer(conn->buffer.prepare(8192)), tok);
            conn->buffer.commit(n);

            // Build a view over the new bytes
            raw_buf
                = std::string_view{reinterpret_cast<const char *>(conn->buffer.data().data()), n};

            // Decode and dispatch each chunk
            for (auto chunk : ChunkIterator(raw_buf.data(), // ptr
                                            raw_buf.size(), // len
                                            chunk_state // state (by reference)
                                            )) {
                bool fin = !is_parsing_chunked_encoding(chunk_state);
                handler(chunk, fin);
            }

            // If we're done with chunked body, break out
            if (!is_parsing_chunked_encoding(chunk_state)) {
                break;
            }

            conn->buffer.clear();
        }
    } else {
        // Read full response body
        boost::beast::http::response<boost::beast::http::string_body> full;
        co_await boost::beast::http::async_read(conn->stream, conn->buffer, full, tok);
        handler(full.body(), true);
    }

    // Return connection to pool if still open
    if (conn->stream.next_layer().socket().is_open() && vec.size() < expected_conns_per_host_) {
        conn->mark_used();
        vec.push_back(std::move(conn));
    }
}

} // namespace http_client
