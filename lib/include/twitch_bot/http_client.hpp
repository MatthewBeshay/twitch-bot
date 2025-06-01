#pragma once

#include <string>
#include <string_view>
#include <map>

#include <boost/json.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>

namespace http_client {

/// JSON result type (Boost.JSON)
using json = boost::json::value;

/**
 * \brief Perform an HTTP GET over TLS and return parsed JSON.
 *
 * \param host     The server host (e.g. "api.example.com").
 * \param port     The server port (e.g. "443").
 * \param target   The request target (e.g. "/v1/data").
 * \param headers  Additional HTTP headers to include (key -> value).
 * \param io_ctx   The asio::io_context to drive I/O.
 * \param ssl_ctx  The asio::ssl::context configured for TLS.
 * \return         An awaitable<json> containing the parsed JSON response.
 * \throws         boost::system::system_error on I/O errors.
 * \throws         std::runtime_error on non-200 status or JSON parse errors.
 */
boost::asio::awaitable<json>
get(std::string_view                              host,
    std::string_view                              port,
    std::string_view                              target,
    std::map<std::string, std::string> const&     headers,
    boost::asio::io_context&                      io_ctx,
    boost::asio::ssl::context&                    ssl_ctx);

/**
 * \brief Perform an HTTP POST over TLS with a string body and return parsed JSON.
 *
 * \param host     The server host (e.g. "api.example.com").
 * \param port     The server port (e.g. "443").
 * \param target   The request target (e.g. "/v1/update").
 * \param body     The request body (already-formatted JSON or form data).
 * \param headers  Additional HTTP headers to include (key -> value).
 * \param io_ctx   The asio::io_context to drive I/O.
 * \param ssl_ctx  The asio::ssl::context configured for TLS.
 * \return         An awaitable<json> containing the parsed JSON response.
 * \throws         boost::system::system_error on I/O errors.
 * \throws         std::runtime_error on non-200 status or JSON parse errors.
 */
boost::asio::awaitable<json>
post(std::string_view                              host,
     std::string_view                              port,
     std::string_view                              target,
     std::string_view                              body,
     std::map<std::string, std::string> const&     headers,
     boost::asio::io_context&                      io_ctx,
     boost::asio::ssl::context&                    ssl_ctx);

} // namespace http_client
