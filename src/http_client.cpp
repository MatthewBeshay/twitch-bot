#include "http_client.hpp"

#include <chrono>
#include <stdexcept>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/this_coro.hpp>
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

//------------------------------------------------------------------------------
// Helper: Establish TCP->SSL connection
//------------------------------------------------------------------------------
asio::awaitable<beast::ssl_stream<beast::tcp_stream>>
make_tls_stream(std::string_view     host,
                std::string_view     port,
                asio::io_context&    io_ctx,
                asio::ssl::context&  ssl_ctx)
{
    auto exec = co_await asio::this_coro::executor;
    tcp::resolver resolver{exec};
    auto endpoints = co_await resolver.async_resolve(
        host, port, asio::use_awaitable);

    beast::tcp_stream tcp_stream{exec};
    tcp_stream.expires_after(std::chrono::seconds(30));
    co_await tcp_stream.async_connect(endpoints, asio::use_awaitable);

    beast::ssl_stream<beast::tcp_stream> stream{
        std::move(tcp_stream), ssl_ctx
    };

    if (!SSL_set_tlsext_host_name(
            stream.native_handle(),
            std::string(host).c_str()))
    {
        boost::system::error_code ec{
            static_cast<int>(::ERR_get_error()),
            asio::error::get_ssl_category()
        };
        throw boost::system::system_error{
            ec, "SNI setup failed for host: " + std::string(host)
        };
    }

    co_await stream.async_handshake(
        asio::ssl::stream_base::client,
        asio::use_awaitable);

    co_return stream;
}

//------------------------------------------------------------------------------
// Helper: Send request & read response
//------------------------------------------------------------------------------
asio::awaitable<http::response<http::string_body>>
exchange(beast::ssl_stream<beast::tcp_stream>& stream,
         http::request<http::string_body>&&    req)
{
    co_await http::async_write(stream, req, asio::use_awaitable);

    beast::flat_buffer buffer;
    http::response<http::string_body> res;
    co_await http::async_read(stream, buffer, res, asio::use_awaitable);

    boost::system::error_code ec_shutdown;
    co_await stream.async_shutdown(
        asio::redirect_error(asio::use_awaitable, ec_shutdown));

    if (ec_shutdown && ec_shutdown != asio::ssl::error::stream_truncated)
        throw boost::system::system_error{ec_shutdown};

    co_return res;
}

} // namespace

//------------------------------------------------------------------------------
// Public GET
//------------------------------------------------------------------------------
asio::awaitable<json> get(
    std::string_view                     host,
    std::string_view                     port,
    std::string_view                     target,
    std::map<std::string, std::string> const& headers,
    asio::io_context&                         io_ctx,
    asio::ssl::context&                       ssl_ctx)
{
    auto stream = co_await make_tls_stream(host, port, io_ctx, ssl_ctx);

    http::request<http::string_body> req{
        http::verb::get, target, 11
    };
    req.set(http::field::host, host);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    for (auto const& [k, v] : headers)
        req.set(k, v);

    auto res = co_await exchange(stream, std::move(req));

    switch (res.result_int())
    {
      case 200: break;
      case 401: throw std::runtime_error("401 Unauthorized");
      case 403: throw std::runtime_error("403 Forbidden");
      // ... handle others ...
      default:  throw std::runtime_error("HTTP " + std::to_string(res.result_int()));
    }

    try {
        co_return bj::parse(res.body());
    }
    catch (boost::system::system_error const& e) {
        std::ostringstream oss;
        oss << "JSON parse error for " << host << target
            << ": " << e.what();
        throw std::runtime_error{ oss.str() };
    }
}

//------------------------------------------------------------------------------
// Public POST
//------------------------------------------------------------------------------
asio::awaitable<json> post(
    std::string_view                     host,
    std::string_view                     port,
    std::string_view                     target,
    std::string_view                     body,
    std::map<std::string, std::string> const& headers,
    asio::io_context&                         io_ctx,
    asio::ssl::context&                       ssl_ctx)
{
    auto stream = co_await make_tls_stream(host, port, io_ctx, ssl_ctx);

    http::request<http::string_body> req{
        http::verb::post, target, 11
    };
    req.set(http::field::host, host);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    for (auto const& [k, v] : headers)
        req.set(k, v);

    req.body() = std::string(body);
    req.prepare_payload();

    auto res = co_await exchange(stream, std::move(req));

    if (res.result() != http::status::ok)
        throw std::runtime_error("HTTP POST failed: " + std::to_string(res.result_int()));

    try {
        co_return bj::parse(res.body());
    }
    catch (boost::system::system_error const& e) {
        std::ostringstream oss;
        oss << "JSON parse error for " << host << target
            << ": " << e.what();
        throw std::runtime_error{ oss.str() };
    }
}

} // namespace http_client
