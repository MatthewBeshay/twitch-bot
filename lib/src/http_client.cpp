// C++ Standard Library
#include <chrono>
#include <string>
#include <system_error>

// 3rd-party
#include <boost/asio/connect.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <openssl/ssl.h>

// Project
#include "http_client.hpp"

namespace http_client {

//----------------------------------------------------------------------------//
// Helper functions & constants with internal linkage
//----------------------------------------------------------------------------//
namespace {

    /// Build a key "host:port" for connection pooling.
    inline std::string make_pool_key(std::string_view host, std::string_view port) noexcept
    {
        std::string key;
        key.reserve(host.size() + 1 + port.size());
        key.append(host);
        key.push_back(':');
        key.append(port);
        return key;
    }

    /// Format an HTTP error message.
    inline std::string
    make_error_msg(std::string_view host, std::string_view target, int status) noexcept
    {
        std::string msg;
        msg.reserve(host.size() + target.size() + 32);
        msg.append(host).append(target).append(" returned ").append(std::to_string(status));
        return msg;
    }

} // namespace (anonymous)

//----------------------------------------------------------------------------//
// client::connection — pooled HTTPS connection
//----------------------------------------------------------------------------//
struct client::connection {
    boost::beast::ssl_stream<boost::beast::tcp_stream> stream;
    static constexpr std::size_t buffer_size = 16 * 1024;
    boost::beast::flat_static_buffer<buffer_size> buffer;

    explicit connection(boost::beast::ssl_stream<boost::beast::tcp_stream>&& s) noexcept
        : stream(std::move(s))
    {
    }

    /// Reset for the next read
    TB_FORCE_INLINE void reset() noexcept
    {
        buffer.clear();
    }
};

//----------------------------------------------------------------------------//
// client ctor / allocator
//----------------------------------------------------------------------------//
client::client(boost::asio::any_io_executor executor,
               boost::asio::ssl::context& ssl_context,
               std::size_t expected_hosts,
               std::size_t expected_conns_per_host) noexcept
    : executor_{executor}
    , ssl_context_{ssl_context}
    , resolver_{executor_}
    , strand_{executor_}
    , expected_conns_per_host_{expected_conns_per_host}
{
    pool_.reserve(expected_hosts);
}

client::allocator_type client::get_allocator() const noexcept
{
    return allocator_type(&handler_buffer_);
}

//----------------------------------------------------------------------------//
// Core HTTP implementation
//----------------------------------------------------------------------------//
boost::asio::awaitable<result> client::perform(boost::beast::http::verb method,
                                               std::string_view host,
                                               std::string_view port,
                                               std::string_view target,
                                               std::string_view body,
                                               http_headers headers) noexcept
{
    // Always run on the strand
    co_await boost::asio::dispatch(strand_, boost::asio::use_awaitable);

    // Try to grab an existing connection from the pool
    auto key = make_pool_key(host, port);
    auto& vec = pool_[key]; // creates vector on‐demand
    std::shared_ptr<connection> conn;
    if (!vec.empty()) {
        conn = std::move(vec.back());
        vec.pop_back();
    }

    // If none, establish a new one
    if (!conn) {
        // DNS + TCP connect
        auto endpoints = co_await resolver_.async_resolve(host, port, boost::asio::use_awaitable);
        boost::beast::tcp_stream tcp{executor_};
        tcp.expires_after(std::chrono::seconds(30));
        co_await tcp.async_connect(endpoints, boost::asio::use_awaitable);

        // TLS handshake
        boost::beast::ssl_stream<boost::beast::tcp_stream> ssl{std::move(tcp), ssl_context_};
        if (!SSL_set_tlsext_host_name(ssl.native_handle(), host.data())) {
            throw std::system_error(static_cast<int>(::ERR_get_error()),
                                    boost::asio::error::get_ssl_category(),
                                    "SNI failure");
        }
        co_await ssl.async_handshake(boost::asio::ssl::stream_base::client,
                                     boost::asio::use_awaitable);

        conn = std::make_shared<connection>(std::move(ssl));
    }

    // Build HTTP request
    boost::beast::http::request<boost::beast::http::string_body> req{method, target, 11};
    req.set(boost::beast::http::field::host, host);
    req.set(boost::beast::http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    for (auto const& h : headers) {
        req.set(h.first, h.second);
    }
    if (method == boost::beast::http::verb::post) {
        auto& b = req.body();
        b.assign(body);
        req.prepare_payload();
    }

    // Send
    conn->reset();
    co_await boost::beast::http::async_write(conn->stream, req, boost::asio::use_awaitable);

    // Receive
    boost::beast::http::response<boost::beast::http::string_body> res;
    co_await boost::beast::http::async_read(conn->stream, conn->buffer, res,
                                            boost::asio::use_awaitable);

    int status = res.result_int();
    if (status < 200 || status >= 300) {
        // recycle before throwing
        vec.push_back(std::move(conn));
        throw std::runtime_error(make_error_msg(host, target, status));
    }

    // Parse JSON
    std::string payload = std::move(res.body());
    json j;
    auto ec = glz::read<json_opts>(j, payload);
    if (ec) {
        vec.push_back(std::move(conn));
        co_return glz::unexpected(ec);
    }

    // Recycle and return
    vec.push_back(std::move(conn));
    co_return j;
}

boost::asio::awaitable<result> client::get(std::string_view host,
                                           std::string_view port,
                                           std::string_view target,
                                           http_headers headers) noexcept
{
    co_return co_await perform(boost::beast::http::verb::get, host, port, target, {}, headers);
}

boost::asio::awaitable<result> client::post(std::string_view host,
                                            std::string_view port,
                                            std::string_view target,
                                            std::string_view body,
                                            http_headers headers) noexcept
{
    co_return co_await perform(boost::beast::http::verb::post, host, port, target, body, headers);
}

} // namespace http_client
