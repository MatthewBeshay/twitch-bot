// C++ standard library
#include <system_error>

// 3rd-party
#include <boost/asio/connect.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <openssl/ssl.h>

#include <glaze/json.hpp>

// Project
#include "http_client.hpp"

namespace http_client {

client::client(boost::asio::any_io_executor executor,
               boost::asio::ssl::context &ssl_context,
               std::size_t expected_hosts,
               std::size_t expected_conns_per_host) noexcept
    : executor_{executor}
    , ssl_context_{&ssl_context}
    , resolver_{executor_}
    , strand_{executor_}
    , expected_conns_per_host_{expected_conns_per_host}
{
    pool_.reserve(expected_hosts);
}

auto client::get_allocator() const noexcept -> allocator_type
{
    return allocator_type(&handler_buffer_);
}

auto client::perform(boost::beast::http::verb method,
                     std::string_view host,
                     std::string_view port,
                     std::string_view target,
                     std::string_view body,
                     http_headers headers) noexcept -> boost::asio::awaitable<result>
{
    // Bind allocator and executor for all operations
    auto tok = boost::asio::bind_allocator(
        get_allocator(), boost::asio::bind_executor(strand_, boost::asio::use_awaitable));

    co_await boost::asio::dispatch(
        strand_, boost::asio::bind_allocator(get_allocator(), boost::asio::use_awaitable));

    // Manage connection pool (similar to stream_get)
    std::string key = detail::make_pool_key(host, port);
    auto it = pool_.find(key);
    std::shared_ptr<connection> conn;

    if (it != pool_.end() && !it->second.empty()) {
        conn = std::move(it->second.back());
        it->second.pop_back();
    }

    if (!conn) {
        if (it == pool_.end()) {
            auto &vec = pool_[key];
            vec.reserve(expected_conns_per_host_);
            it = pool_.find(key);
        }

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

    // Build and send request
    boost::beast::http::request<boost::beast::http::string_body> req{method, target,
                                                                     k_http_version};
    req.set(boost::beast::http::field::host, host);
    req.set(boost::beast::http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    if (method == boost::beast::http::verb::post) {
        req.body() = body;
        req.prepare_payload();
    }
    for (auto &h : headers) {
        req.set(h.first, h.second);
    }
    conn->reset();
    conn->stream.next_layer().expires_after(k_http_write_timeout);
    co_await boost::beast::http::async_write(conn->stream, req, tok);

    // Read response
    conn->stream.next_layer().expires_after(k_http_read_timeout);
    boost::beast::http::response<boost::beast::http::string_body> res;
    co_await boost::beast::http::async_read(conn->stream, conn->buffer, res, tok);

    int status = res.result_int();
    if (status < 200 || status >= 300) {
        auto &vec = pool_[key];
        if (conn->stream.next_layer().socket().is_open() && vec.size() < expected_conns_per_host_) {
            conn->mark_used();
            vec.push_back(std::move(conn));
        }
        throw std::runtime_error(detail::make_error_msg(host, target, status));
    }

    // Zero-copy JSON parsing with glaze
    auto &body_str = res.body();
    body_str.reserve(body_str.size() + 1); // ensure room for null terminator
    body_str.push_back('\0'); // in-place null-termination
    std::string_view sv{// view over the JSON payload (excluding our '\0')
                        body_str.data(), body_str.size() - 1};

    json j;
    glz::error_ctx ec = glz::read<json_opts>(j, sv);
    if (ec) {
        auto &vec = pool_[key];
        if (conn->stream.next_layer().socket().is_open() && vec.size() < expected_conns_per_host_) {
            conn->mark_used();
            vec.push_back(std::move(conn));
        }
        co_return glz::unexpected(ec);
    }

    // Return connection to pool
    {
        auto &vec = pool_[key];
        if (conn->stream.next_layer().socket().is_open() && vec.size() < expected_conns_per_host_) {
            conn->mark_used();
            vec.push_back(std::move(conn));
        }
    }

    co_return j;
}

auto client::get(std::string_view host,
                 std::string_view port,
                 std::string_view target,
                 http_headers headers) noexcept -> boost::asio::awaitable<result>
{
    co_return co_await perform(boost::beast::http::verb::get, host, port, target, {}, headers);
}

auto client::post(std::string_view host,
                  std::string_view port,
                  std::string_view target,
                  std::string_view body,
                  http_headers headers) noexcept -> boost::asio::awaitable<result>
{
    co_return co_await perform(boost::beast::http::verb::post, host, port, target, body, headers);
}

} // namespace http_client
