#include "http_client.hpp"

#include <chrono>
#include <string>
#include <sstream>
#include <stdexcept>

#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/json.hpp>
#include <boost/system/system_error.hpp>

#include <openssl/ssl.h>

namespace http_client {
namespace {

namespace asio  = boost::asio;
namespace beast = boost::beast;
namespace http  = beast::http;
namespace bj    = boost::json;
using tcp       = asio::ip::tcp;
using error_code = boost::system::error_code;

/// \brief Establish a TCP->SSL connection to (host,port). Returns the SSL stream.
/// \throws system_error on resolution, connect, or handshake failures.
asio::awaitable<beast::ssl_stream<beast::tcp_stream>> make_tls_stream(
    std::string_view    host,
    std::string_view    port,
    asio::io_context&   io_ctx,
    asio::ssl::context& ssl_ctx)
{
    // Get executor from current coroutine context
    auto exec = co_await asio::this_coro::executor;

    // Resolve host:port
    tcp::resolver resolver(exec);
    auto endpoints = co_await resolver.async_resolve(
        std::string(host), std::string(port),
        asio::use_awaitable
    );

    // Establish raw TCP connection
    beast::tcp_stream tcp_strm{exec};
    tcp_strm.expires_after(std::chrono::seconds(30));
    co_await tcp_strm.async_connect(endpoints, asio::use_awaitable);

    // Wrap in SSL
    beast::ssl_stream<beast::tcp_stream> ssl_strm{
        std::move(tcp_strm), ssl_ctx
    };

    // Set SNI hostname
    if (!SSL_set_tlsext_host_name(
            ssl_strm.native_handle(),
            host.data()))
    {
        error_code ec{
            static_cast<int>(::ERR_get_error()),
            asio::error::get_ssl_category()
        };
        throw boost::system::system_error{
            ec, "Failed to set SNI host: " + std::string(host)
        };
    }

    // Perform SSL handshake
    co_await ssl_strm.async_handshake(
        asio::ssl::stream_base::client,
        asio::use_awaitable
    );

    co_return ssl_strm;
}

/// \brief Send the HTTP request and receive a string_body response.
/// \throws system_error on I/O errors or shutdown errors.
asio::awaitable<http::response<http::string_body>> exchange(
    beast::ssl_stream<beast::tcp_stream>&        ssl_strm,
    http::request<http::string_body>&&           req)
{
    // Write the request
    co_await http::async_write(ssl_strm, req, asio::use_awaitable);

    // Read the response
    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    co_await http::async_read(ssl_strm, buffer, res, asio::use_awaitable);

    // Graceful SSL shutdown (ignore truncated)
    error_code ec_shutdown;
    co_await ssl_strm.async_shutdown(
        asio::redirect_error(asio::use_awaitable, ec_shutdown)
    );
    if (ec_shutdown && ec_shutdown != asio::ssl::error::stream_truncated) {
        throw boost::system::system_error{ec_shutdown};
    }

    co_return res;
}

} // namespace (anonymous)

//------------------------------------------------------------------------------
// Public GET
//------------------------------------------------------------------------------
asio::awaitable<http_client::json> get(
    std::string_view                              host,
    std::string_view                              port,
    std::string_view                              target,
    std::map<std::string, std::string> const&     headers,
    asio::io_context&                             io_ctx,
    asio::ssl::context&                           ssl_ctx)
{
    // Create an SSL stream connected to host:port
    auto ssl_strm = co_await make_tls_stream(host, port, io_ctx, ssl_ctx);

    // Build GET request
    http::request<http::string_body> req{
        http::verb::get, std::string(target), 11
    };
    req.set(http::field::host, host);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    for (auto const& [k, v] : headers) {
        req.set(k, v);
    }

    // Send request and get response
    auto res = co_await exchange(ssl_strm, std::move(req));

    // Check status code
    auto status = res.result_int();
    if (status != 200) {
        std::ostringstream oss;
        oss << "HTTP GET " << host << target << " returned " << status;
        throw std::runtime_error{oss.str()};
    }

    // Parse JSON
    try {
        co_return bj::parse(res.body());
    }
    catch (const boost::system::system_error& e) {
        std::ostringstream oss;
        oss << "JSON parse error for " << host << target
            << ": " << e.what();
        throw std::runtime_error{oss.str()};
    }
}

//------------------------------------------------------------------------------
// Public POST
//------------------------------------------------------------------------------
asio::awaitable<http_client::json> post(
    std::string_view                              host,
    std::string_view                              port,
    std::string_view                              target,
    std::string_view                              body,
    std::map<std::string, std::string> const&     headers,
    asio::io_context&                             io_ctx,
    asio::ssl::context&                           ssl_ctx)
{
    // Create an SSL stream connected to host:port
    auto ssl_strm = co_await make_tls_stream(host, port, io_ctx, ssl_ctx);

    // Build POST request
    http::request<http::string_body> req{
        http::verb::post, std::string(target), 11
    };
    req.set(http::field::host, host);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    for (auto const& [k, v] : headers) {
        req.set(k, v);
    }
    req.body() = std::string(body);
    req.prepare_payload();

    // Send request and get response
    auto res = co_await exchange(ssl_strm, std::move(req));

    // Check status code
    auto status = res.result_int();
    if (status != 200) {
        std::ostringstream oss;
        oss << "HTTP POST " << host << target << " returned " << status;
        throw std::runtime_error{oss.str()};
    }

    // Parse JSON
    try {
        co_return bj::parse(res.body());
    }
    catch (const boost::system::system_error& e) {
        std::ostringstream oss;
        oss << "JSON parse error for " << host << target
            << ": " << e.what();
        throw std::runtime_error{oss.str()};
    }
}

} // namespace http_client
