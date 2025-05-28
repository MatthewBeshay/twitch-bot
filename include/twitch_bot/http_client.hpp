#pragma once

#include <string_view>
#include <map>

#include <boost/json.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>

namespace http_client {

/// JSON result type (Boost.JSON)
using json = boost::json::value;

/// Asynchronous HTTP GET over TLS, returning parsed JSON.
/// @throws system_error on network/handshake failures
/// @throws runtime_error on non-200 status or JSON parse errors
boost::asio::awaitable<json>
get(std::string_view                     host,
    std::string_view                     port,
    std::string_view                     target,
    std::map<std::string, std::string> const& headers,
    boost::asio::io_context&                   io_ctx,
    boost::asio::ssl::context&                 ssl_ctx);

/// Asynchronous HTTP POST over TLS with a string body, returning parsed JSON.
/// @throws same as get()
boost::asio::awaitable<json>
post(std::string_view                     host,
     std::string_view                     port,
     std::string_view                     target,
     std::string_view                     body,
     std::map<std::string, std::string> const& headers,
     boost::asio::io_context&                   io_ctx,
     boost::asio::ssl::context&                 ssl_ctx);

} // namespace http_client
