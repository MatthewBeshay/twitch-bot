#include "http_client.hpp"

#include <chrono>
#include <stdexcept>
#include <string>
#include <string_view>

#include <boost/asio/connect.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>

#include <openssl/ssl.h>

namespace http_client {
namespace detail {

/// Build pool key "host:port".
inline std::string makePoolKey(std::string_view host,
                               std::string_view port) noexcept
{
    std::string key;
    key.reserve(host.size() + 1 + port.size());
    key.append(host);
    key.push_back(':');
    key.append(port);
    return key;
}

/// Format an HTTP-error message.
inline std::string makeErrorMsg(std::string_view host,
                                std::string_view target,
                                int               status) noexcept
{
  std::string msg;
  msg.reserve(host.size() + target.size() + 32);
  msg.append(host)
     .append(target)
     .append(" returned ")
     .append(std::to_string(status));
  return msg;
}

} // namespace detail

// Represents one persistent TLS connection.
struct Client::Connection {
  boost::beast::ssl_stream<boost::beast::tcp_stream> stream;
  boost::beast::flat_buffer                   buffer;

  explicit Connection(boost::beast::ssl_stream<boost::beast::tcp_stream>&& s) noexcept
    : stream(std::move(s)), buffer() {}

  /// Clear buffers before reuse.
  void reset() noexcept { buffer.clear(); }
};

Client::Client(boost::asio::any_io_executor executor,
               boost::asio::ssl::context&   sslCtx) noexcept
  : executor_(executor),
    sslCtx_(sslCtx),
    resolver_(executor),
    strand_(executor)
{}

boost::asio::awaitable<Result>
Client::perform(boost::beast::http::verb method,
                std::string_view       host,
                std::string_view       port,
                std::string_view       target,
                std::string_view       body,
                HttpHeaders            headers) noexcept
{
  // 1) Borrow or create connection
  auto key  = detail::makePoolKey(host, port);
  std::shared_ptr<Connection> conn;

  co_await boost::asio::post(strand_, boost::asio::use_awaitable);
  auto &vec = pool_[key];
  if (!vec.empty()) {
    conn = std::move(vec.back());
    vec.pop_back();
  }

  if (!conn) {
    // Resolve DNS
    auto endpoints = co_await resolver_.async_resolve(
      host, port, boost::asio::use_awaitable);

    // TCP connect
    boost::beast::tcp_stream tcp{executor_};
    tcp.expires_after(std::chrono::seconds{30});
    co_await tcp.async_connect(endpoints,
                               boost::asio::use_awaitable);

    // TLS handshake with SNI
    boost::beast::ssl_stream<boost::beast::tcp_stream> ssl{std::move(tcp), sslCtx_};
    if (!SSL_set_tlsext_host_name(
          ssl.native_handle(), host.data())) {
      throw std::system_error{
        static_cast<int>(::ERR_get_error()),
        boost::asio::error::get_ssl_category(),
        "SNI failure"
      };
    }
    co_await ssl.async_handshake(
      boost::asio::ssl::stream_base::client,
      boost::asio::use_awaitable);

    conn = std::make_shared<Connection>(std::move(ssl));
  }

  // 2) Build request
  boost::beast::http::request<boost::beast::http::string_body> req{method, target, 11};
  req.set(boost::beast::http::field::host,       host);
  req.set(boost::beast::http::field::user_agent, BOOST_BEAST_VERSION_STRING);
  for (auto const &h : headers) {
    req.set(h.first, h.second);
  }
  if (method == boost::beast::http::verb::post) {
    auto &b = req.body();
    b.clear();
    b.reserve(body.size());
    b.append(body);
    req.prepare_payload();
  }

  conn->reset();

  // 3) Send + receive
  co_await boost::beast::http::async_write(conn->stream, req,
                                           boost::asio::use_awaitable);
  boost::beast::http::response<boost::beast::http::string_body> res;
  co_await boost::beast::http::async_read(conn->stream, conn->buffer, res,
                                          boost::asio::use_awaitable);

  int status = res.result_int();
  if (status < 200 || status >= 300) {
    // Return connection on error
    co_await boost::asio::post(strand_, boost::asio::use_awaitable);
    pool_[key].push_back(std::move(conn));
    throw std::runtime_error(
      detail::makeErrorMsg(host, target, status));
  }

  // 4) Parse JSON with your JsonOpts
  std::string bodyBuf = std::move(res.body());

  // Create an empty Json object
  Json j{};

  // Read using your custom options
  auto ec = glz::read<JsonOpts>(j, bodyBuf);
  if (ec) {
      // Return the connection to the pool before bailing
      co_await boost::asio::post(strand_, boost::asio::use_awaitable);
      pool_[key].push_back(std::move(conn));
      co_return glz::unexpected(ec);
  }

  // 5) Recycle the connection and return the parsed JSON
  co_await boost::asio::post(strand_, boost::asio::use_awaitable);
  pool_[key].push_back(std::move(conn));
  co_return j;
}

boost::asio::awaitable<Result>
Client::get(std::string_view host,
            std::string_view port,
            std::string_view target,
            HttpHeaders      headers) noexcept
{
  co_return co_await perform(
    boost::beast::http::verb::get,
    host, port, target, {}, headers);
}

boost::asio::awaitable<Result>
Client::post(std::string_view host,
             std::string_view port,
             std::string_view target,
             std::string_view body,
             HttpHeaders      headers) noexcept
{
  co_return co_await perform(
    boost::beast::http::verb::post,
    host, port, target, body, headers);
}

} // namespace http_client
