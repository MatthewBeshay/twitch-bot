#pragma once

#include <map>
#include <string>
#include <string_view>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/json.hpp>

namespace http_client {

using json = boost::json::value;

/// Perform an HTTP GET over TLS and return parsed JSON.
/// @param host     The server host (for example, "api.example.com").
/// @param port     The server port (for example, "443").
/// @param target   The request target (for example, "/v1/data").
/// @param headers  Additional HTTP headers to include (key -> value).
/// @param io_ctx   The asio::io_context to drive I/O.
/// @param ssl_ctx  The asio::ssl::context configured for TLS.
/// @return         An awaitable<json> containing the parsed JSON response.
/// @throws boost::system::system_error on I/O errors.
/// @throws std::runtime_error on non-200 status or JSON parse errors.
boost::asio::awaitable<json>
get(std::string_view                              host,
    std::string_view                              port,
    std::string_view                              target,
    const std::map<std::string, std::string>&     headers,
    boost::asio::io_context&                      io_ctx,
    boost::asio::ssl::context&                    ssl_ctx);

/// Perform an HTTP POST over TLS with a string body and return parsed JSON.
/// @param host     The server host (for example, "api.example.com").
/// @param port     The server port (for example, "443").
/// @param target   The request target (for example, "/v1/update").
/// @param body     The request body (already formatted JSON or form data).
/// @param headers  Additional HTTP headers to include (key -> value).
/// @param io_ctx   The asio::io_context to drive I/O.
/// @param ssl_ctx  The asio::ssl::context configured for TLS.
/// @return         An awaitable<json> containing the parsed JSON response.
/// @throws boost::system::system_error on I/O errors.
/// @throws std::runtime_error on non-200 status or JSON parse errors.
boost::asio::awaitable<json>
post(std::string_view                              host,
     std::string_view                              port,
     std::string_view                              target,
     std::string_view                              body,
     const std::map<std::string, std::string>&     headers,
     boost::asio::io_context&                      io_ctx,
     boost::asio::ssl::context&                    ssl_ctx);

} // namespace http_client
