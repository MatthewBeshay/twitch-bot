#include "http_client.hpp"

#include <chrono>
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

#include <openssl/ssl.h>

namespace http_client {
namespace {

namespace asio   = boost::asio;
namespace beast  = boost::beast;
namespace http   = beast::http;
namespace bj     = boost::json;
using tcp        = asio::ip::tcp;
using error_code = boost::system::error_code;

/// Build a unique key for our connection pool.
static std::string make_pool_key(std::string_view host,
                                 std::string_view port)
{
    std::string key;
    key.reserve(host.size() + 1 + port.size());
    key.append(host);
    key.push_back(':');
    key.append(port);
    return key;
}

/// Construct a concise error message without extra temporaries.
static std::string make_error_msg(std::string_view host,
                                  std::string_view target,
                                  int status)
{
    std::string msg;
    msg.reserve(host.size() + target.size() + 32);
    msg.append(host)
       .append(target)
       .append(" returned ")
       .append(std::to_string(status));
    return msg;
}

} // anonymous

// Represents a single persistent TLS connection with reusable buffers
// and its own Boost-JSON parser (which retains its allocations).
struct Client::Connection {
    beast::ssl_stream<beast::tcp_stream> stream;
    beast::flat_buffer                   buffer;
    bj::parser                           parser;

    explicit Connection(beast::ssl_stream<beast::tcp_stream>&& s)
      : stream(std::move(s))
      , buffer()
      , parser()   // parser manages its own storage internally
    {}

    /// Clear buffers and parser state for the next request.
    void reset()
    {
        buffer.clear();
        parser.reset();
    }
};

Client::Client(boost::asio::any_io_executor exec,
               boost::asio::ssl::context& ssl_ctx)
  : executor_(exec),
    ssl_ctx_(ssl_ctx),
    resolver_(exec),
    strand_(exec)
{}

/// Core implementation behind both GET and POST.
boost::asio::awaitable<boost::json::value> Client::perform(
    boost::beast::http::verb method,
    std::string_view host,
    std::string_view port,
    std::string_view target,
    std::string_view body,
    Headers headers)
{
    auto key = make_pool_key(host, port);
    std::shared_ptr<Connection> conn;

    // — Borrow or create a connection —
    co_await asio::post(strand_, asio::use_awaitable);
    {
        auto &vec = pool_[key];
        if (!vec.empty()) {
            conn = vec.back();
            vec.pop_back();
        }
    }

    if (!conn) {
        // DNS + TCP connect
        auto endpoints = co_await resolver_
            .async_resolve(host, port, asio::use_awaitable);

        beast::tcp_stream tcp_strm(executor_);
        tcp_strm.expires_after(std::chrono::seconds(30));
        co_await tcp_strm.async_connect(endpoints, asio::use_awaitable);

        // TLS handshake with SNI
        beast::ssl_stream<beast::tcp_stream> ssl_strm(
            std::move(tcp_strm), ssl_ctx_);
        if (!SSL_set_tlsext_host_name(
                ssl_strm.native_handle(), host.data()))
        {
            error_code ec{
                static_cast<int>(::ERR_get_error()),
                asio::error::get_ssl_category()
            };
            throw boost::system::system_error(ec, "SNI failure");
        }
        co_await ssl_strm
            .async_handshake(asio::ssl::stream_base::client,
                             asio::use_awaitable);

        conn = std::make_shared<Connection>(std::move(ssl_strm));
    }

    // — Prepare request —
    http::request<http::string_body> req{ method, target, 11 };
    req.set(http::field::host, host);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    for (auto const &h : headers) {
        req.set(h.first, h.second);
    }
    if (method == http::verb::post) {
        auto &b = req.body();
        b.clear();
        b.reserve(body.size());
        b.append(body);
        req.prepare_payload();
    }

    conn->reset();

    // — Send & receive —
    co_await http::async_write(conn->stream, req, asio::use_awaitable);

    http::response<http::string_body> res;
    co_await http::async_read(
        conn->stream, conn->buffer, res, asio::use_awaitable);

    int status = res.result_int();
    if (status < 200 || status >= 300) {
        // recycle on error
        co_await asio::post(strand_, asio::use_awaitable);
        pool_[key].push_back(conn);
        throw std::runtime_error(
            make_error_msg(host, target, status));
    }

    // — Parse JSON with parser —
    bj::value jv;
    try {
        conn->parser.write(res.body());
        jv = conn->parser.release();
    }
    catch (boost::system::system_error const& e) {
        // return connection to pool (no co_await inside catch)
        asio::post(strand_, [this, key, conn = std::move(conn)]() mutable {
            pool_[key].push_back(std::move(conn));
        });
        throw std::runtime_error(
            "JSON parse error for "
            + std::string(host)
            + std::string(target)
            + ": "
            + e.what()
        );
    }

    // — Return connection to pool —
    co_await asio::post(strand_, asio::use_awaitable);
    pool_[key].push_back(conn);

    co_return jv;
}

boost::asio::awaitable<boost::json::value> Client::get(
    std::string_view host,
    std::string_view port,
    std::string_view target,
    Headers headers)
{
    co_return co_await perform(
        boost::beast::http::verb::get,
        host, port, target, {}, headers);
}

boost::asio::awaitable<boost::json::value> Client::post(
    std::string_view host,
    std::string_view port,
    std::string_view target,
    std::string_view body,
    Headers headers)
{
    co_return co_await perform(
        boost::beast::http::verb::post,
        host, port, target, body, headers);
}

} // namespace http_client
