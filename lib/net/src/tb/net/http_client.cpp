// C++ standard library
#include <system_error>

// Boost.Asio
#include <boost/asio/connect.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

// OpenSSL
#include <openssl/err.h>
#include <openssl/ssl.h>

// Glaze
#include <glaze/json.hpp>

// GSL
#include <gsl/gsl>

// Project
#include <tb/net/http_client.hpp>

namespace http_client {

client::client(boost::asio::any_io_executor executor,
               boost::asio::ssl::context& ssl_context,
               std::size_t expected_hosts,
               std::size_t expected_conns_per_host) noexcept
    : executor_{executor}
    , ssl_context_{&ssl_context}
    , resolver_{executor_}
    , strand_{executor_}
    , expected_conns_per_host_{expected_conns_per_host}
{
    Expects(ssl_context_ != nullptr);
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
                     http_headers headers) -> boost::asio::awaitable<result>
{
    namespace asio = boost::asio;
    namespace beast = boost::beast;
    namespace http = beast::http;

    // Bind allocator and executor for all operations
    auto tok
        = asio::bind_allocator(get_allocator(), asio::bind_executor(strand_, asio::use_awaitable));

    // Manage connection pool (similar to stream_get)
    std::string key = detail::make_pool_key(host, port);
    auto it = pool_.find(key);
    std::shared_ptr<connection> conn;

    if (it != pool_.end() && !it->second.empty()) {
        conn = std::move(it->second.back());
        it->second.pop_back();
    }

    // Drop idle connections
    if (conn && std::chrono::steady_clock::now() - conn->last_used > k_pool_idle_timeout) {
        conn.reset();
    }

    if (!conn) {
        if (it == pool_.end()) {
            auto& vec = pool_[key];
            vec.reserve(expected_conns_per_host_);
            it = pool_.find(key);
        }

        // Resolve DNS
        auto endpoints = co_await resolver_.async_resolve(host, port, tok);

        // Establish TCP connection
        beast::tcp_stream tcp(executor_);
        beast::get_lowest_layer(tcp).expires_after(k_tcp_connect_timeout);
        co_await tcp.async_connect(endpoints, tok);

        // Disable Nagle's algorithm
        beast::get_lowest_layer(tcp).socket().set_option(asio::ip::tcp::no_delay{true});

        // Upgrade to TLS
        typename connection::ssl_stream ssl{std::move(tcp), *ssl_context_};
        // SNI requires NUL-terminated host
        const std::string host_sni{host};
        if (!::SSL_set_tlsext_host_name(ssl.native_handle(), host_sni.c_str())) {
            throw std::system_error{static_cast<int>(::ERR_get_error()),
                                    asio::error::get_ssl_category(), "SNI failure"};
        }
        beast::get_lowest_layer(ssl).expires_after(k_handshake_timeout);
        co_await ssl.async_handshake(asio::ssl::stream_base::client, tok);

        conn = std::make_shared<connection>(std::move(ssl));
    }

    // Ensure the connection is returned to the pool on all exit paths
    auto& vec = pool_[key];
    auto return_to_pool = gsl::finally([&] {
        if (conn && beast::get_lowest_layer(conn->stream).socket().is_open()
            && vec.size() < expected_conns_per_host_) {
            conn->mark_used();
            vec.push_back(std::move(conn));
        }
    });

    // Build and send request
    http::request<http::string_body> req{method, target, k_http_version};
    req.set(http::field::host, host);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    if (method == http::verb::post) {
        req.body() = std::string{body};
        req.prepare_payload();
    }
    for (auto& h : headers)
        req.set(h.first, h.second);

    conn->reset();
    beast::get_lowest_layer(conn->stream).expires_after(k_http_write_timeout);
    co_await http::async_write(conn->stream, req, tok);

    // Read response
    beast::get_lowest_layer(conn->stream).expires_after(k_http_read_timeout);
    http::response<http::string_body> res;
    co_await http::async_read(conn->stream, conn->buffer, res, tok);

    const int status = res.result_int();
    if (status < 200 || status >= 300) {
        throw std::runtime_error(detail::make_error_msg(host, target, status));
    }

    // Zero-copy-ish JSON parsing with glaze (use NTTP opts)
    auto& body_str = res.body();
    body_str.reserve(body_str.size() + 1); // ensure room for null terminator
    body_str.push_back('\0'); // in-place null-termination
    std::string_view sv{body_str.data(), body_str.size() - 1}; // exclude the '\0'

    json j{};
    if (glz::error_ctx ec = glz::read<json_opts>(j, sv); ec) {
        co_return glz::unexpected(ec);
    }

    co_return j;
}

auto client::get(std::string_view host,
                 std::string_view port,
                 std::string_view target,
                 http_headers headers) -> boost::asio::awaitable<result>
{
    co_return co_await perform(boost::beast::http::verb::get, host, port, target, {}, headers);
}

auto client::post(std::string_view host,
                  std::string_view port,
                  std::string_view target,
                  std::string_view body,
                  http_headers headers) -> boost::asio::awaitable<result>
{
    co_return co_await perform(boost::beast::http::verb::post, host, port, target, body, headers);
}

} // namespace http_client
