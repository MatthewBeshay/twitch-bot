#pragma once

#include "utils/transparent_string.hpp"

#include <string>
#include <string_view>
#include <span>
#include <unordered_map>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/beast/http.hpp>

#include <glaze/json.hpp>

namespace http_client {

/// JSON value returned by HTTP client.
using Json   = glz::json_t;
/// Result type: parsed JSON or an error context on failure.
using Result = glz::expected<Json, glz::error_ctx>;

/// Compile-time JSON parse options (null-terminated, allow unknown keys, minified).
inline constexpr glz::opts JsonOpts {
    .null_terminated       = true,
    .error_on_unknown_keys = false,
    .minified              = true
};

/// Single HTTP header (name, value).
using HttpHeader  = std::pair<std::string_view, std::string_view>;
/// Sequence of HTTP headers.
using HttpHeaders = std::span<const HttpHeader>;

/// High-performance HTTP/1.1-over-TLS client:
///  - persistent connection pooling (keep-alive)
///  - reusable Beast buffers
///  - minimal per-request allocations
class Client {
public:
  /// Construct on the given executor and SSL context.
  Client(boost::asio::any_io_executor executor,
         boost::asio::ssl::context&   sslCtx) noexcept;

  /// Perform HTTP GET and parse JSON.
  [[nodiscard]]
  boost::asio::awaitable<Result>
  get(std::string_view host,
      std::string_view port,
      std::string_view target,
      HttpHeaders      headers = {}) noexcept;

  /// Perform HTTP POST with string body and parse JSON.
  [[nodiscard]]
  boost::asio::awaitable<Result>
  post(std::string_view host,
       std::string_view port,
       std::string_view target,
       std::string_view body,
       HttpHeaders      headers = {}) noexcept;

private:
  struct Connection;

  /// Core implementation behind GET and POST.
  [[nodiscard]]
  boost::asio::awaitable<Result>
  perform(boost::beast::http::verb method,
          std::string_view       host,
          std::string_view       port,
          std::string_view       target,
          std::string_view       body,
          HttpHeaders            headers) noexcept;

  boost::asio::any_io_executor                      executor_;
  boost::asio::ssl::context&                        sslCtx_;
  boost::asio::ip::tcp::resolver                    resolver_;
  boost::asio::strand<boost::asio::any_io_executor> strand_;

  // Idle connections keyed by "host:port"
  std::unordered_map<
    std::string,
    std::vector<std::shared_ptr<Connection>>,
    TransparentStringHash,
    TransparentStringEq
  > pool_;
};

} // namespace http_client
