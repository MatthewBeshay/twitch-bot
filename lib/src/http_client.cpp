#include "http_client.hpp"

#include <chrono>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
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

/// Establish a TCP→SSL connection to (host, port) and return a ready-to-use SSL stream.
/// @param host    The server host (for example, "api.example.com").
/// @param port    The server port (for example, "443").
/// @param io_ctx  The asio::io_context to drive I/O.
/// @param ssl_ctx The asio::ssl::context configured for TLS.
/// @return        An awaitable yielding an SSL stream on success.
/// @throws boost::system::system_error on resolution, connect, or handshake failure.
asio::awaitable<beast::ssl_stream<beast::tcp_stream>> make_tls_stream(
    std::string_view    host,
    std::string_view    port,
    asio::io_context&   /*io_ctx*/,
    asio::ssl::context& ssl_ctx)
{
    // Get executor from current coroutine context.
    auto exec = co_await asio::this_coro::executor;

    // Resolve host:port.
    tcp::resolver resolver(exec);
    auto endpoints = co_await resolver.async_resolve(
        std::string(host),
        std::string(port),
        asio::use_awaitable
    );

    // Establish raw TCP connection.
    beast::tcp_stream tcp_strm{exec};
    tcp_strm.expires_after(std::chrono::seconds(30));
    co_await tcp_strm.async_connect(endpoints, asio::use_awaitable);

    // Wrap the connected TCP socket in an SSL stream.
    beast::ssl_stream<beast::tcp_stream> ssl_strm{std::move(tcp_strm), ssl_ctx};

    // Set SNI hostname on the SSL stream.
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

    // Perform SSL handshake.
    co_await ssl_strm.async_handshake(
        asio::ssl::stream_base::client,
        asio::use_awaitable
    );

    co_return ssl_strm;
}

/// Send the HTTP request and receive a string_body response over the given SSL stream.
/// @param ssl_strm The SSL stream on which to send the request.
/// @param req      The HTTP request to send (string_body).
/// @return         An awaitable yielding the HTTP response on success.
/// @throws boost::system::system_error on I/O or shutdown errors.
asio::awaitable<http::response<http::string_body>> exchange(
    beast::ssl_stream<beast::tcp_stream>& ssl_strm,
    http::request<http::string_body>&&    req)
{
    // Write the request to the SSL stream.
    co_await http::async_write(ssl_strm, req, asio::use_awaitable);

    // Read the response into a buffer and parse it.
    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    co_await http::async_read(ssl_strm, buffer, res, asio::use_awaitable);

    // Perform a graceful SSL shutdown (ignore truncated errors).
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

/// Perform an HTTP GET over TLS and return parsed JSON.
/// @param host    The server host (for example, "api.example.com").
/// @param port    The server port (for example, "443").
/// @param target  The request target (for example, "/v1/data").
/// @param headers Additional HTTP headers to include (key → value).
/// @param io_ctx  The asio::io_context to drive I/O.
/// @param ssl_ctx The asio::ssl::context configured for TLS.
/// @return        An awaitable yielding the parsed JSON response.
/// @throws boost::system::system_error on I/O errors.
/// @throws std::runtime_error on non-200 status or JSON parse errors.
asio::awaitable<json> get(
    std::string_view                              host,
    std::string_view                              port,
    std::string_view                              target,
    std::map<std::string, std::string> const&     headers,
    asio::io_context&                             io_ctx,
    asio::ssl::context&                           ssl_ctx)
{
    // Create an SSL stream connected to host:port.
    auto ssl_strm = co_await make_tls_stream(host, port, io_ctx, ssl_ctx);

    // Build the GET request.
    http::request<http::string_body> req{
        http::verb::get,
        std::string(target),
        11
    };
    req.set(http::field::host, host);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    for (auto const& [key, value] : headers) {
        req.set(key, value);
    }

    // Send the request and receive the response.
    auto res = co_await exchange(ssl_strm, std::move(req));

    // Check for HTTP 200 OK.
    auto status = res.result_int();
    if (status != 200) {
        std::ostringstream oss;
        oss << "HTTP GET " << host << target << " returned " << status;
        throw std::runtime_error{oss.str()};
    }

    // Parse and return JSON from the response body.
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

/// Perform an HTTP POST over TLS with a string body and return parsed JSON.
/// @param host    The server host (for example, "api.example.com").
/// @param port    The server port (for example, "443").
/// @param target  The request target (for example, "/v1/update").
/// @param body    The request body (already formatted JSON or form data).
/// @param headers Additional HTTP headers to include (key → value).
/// @param io_ctx  The asio::io_context to drive I/O.
/// @param ssl_ctx The asio::ssl::context configured for TLS.
/// @return        An awaitable yielding the parsed JSON response.
/// @throws boost::system::system_error on I/O errors.
/// @throws std::runtime_error on non-200 status or JSON parse errors.
asio::awaitable<json> post(
    std::string_view                              host,
    std::string_view                              port,
    std::string_view                              target,
    std::string_view                              body,
    std::map<std::string, std::string> const&     headers,
    asio::io_context&                             io_ctx,
    asio::ssl::context&                           ssl_ctx)
{
    // Create an SSL stream connected to host:port.
    auto ssl_strm = co_await make_tls_stream(host, port, io_ctx, ssl_ctx);

    // Build the POST request.
    http::request<http::string_body> req{
        http::verb::post,
        std::string(target),
        11
    };
    req.set(http::field::host, host);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    for (auto const& [key, value] : headers) {
        req.set(key, value);
    }
    req.body() = std::string(body);
    req.prepare_payload();

    // Send the request and receive the response.
    auto res = co_await exchange(ssl_strm, std::move(req));

    // Check for HTTP 200 OK.
    auto status = res.result_int();
    if (status != 200) {
        std::ostringstream oss;
        oss << "HTTP POST " << host << target << " returned " << status;
        throw std::runtime_error{oss.str()};
    }

    // Parse and return JSON from the response body.
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
