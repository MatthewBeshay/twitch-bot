// http_client.hpp
#pragma once

#include <string>
#include <string_view>
#include <map>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>

namespace http_client {

    /// @brief Perform a synchronous HTTP GET over TLS.
    /// @param host     e.g. "api.example.com"
    /// @param port     e.g. "443"
    /// @param target   e.g. "/v1/resource"
    /// @param headers  Additional HTTP headers
    /// @param ioc      Asio I/O context (must be running or run() later)
    /// @param ssl_ctx  Asio SSL context configured for client
    /// @returns        HTTP response body (only when status == 200)
    /// @throws         boost::system::system_error on network/handshake failures
    /// @throws         std::runtime_error on SNI setup failure or non-200 status
    std::string get(
        std::string_view                       host,
        std::string_view                       port,
        std::string_view                       target,
        std::map<std::string, std::string> const& headers,
        boost::asio::io_context& ioc,
        boost::asio::ssl::context& ssl_ctx);

    /// @brief Perform a synchronous HTTP POST over TLS.
    /// @param host     e.g. "api.example.com"
    /// @param port     e.g. "443"
    /// @param target   e.g. "/v1/resource"
    /// @param body     Request payload
    /// @param headers  Additional HTTP headers
    /// @param ioc      Asio I/O context
    /// @param ssl_ctx  Asio SSL context
    /// @returns        HTTP response body (only when status == 200)
    /// @throws         Same as get()
    std::string post(
        std::string_view                       host,
        std::string_view                       port,
        std::string_view                       target,
        std::string_view                       body,
        std::map<std::string, std::string> const& headers,
        boost::asio::io_context& ioc,
        boost::asio::ssl::context& ssl_ctx);

} // namespace http_client
