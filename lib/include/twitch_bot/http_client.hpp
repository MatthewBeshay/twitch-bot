#pragma once

#include "utils/transparent_string.hpp"

#include <string>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/strand.hpp>

#include <boost/json/value.hpp>

#include <boost/beast/http.hpp>

namespace http_client {

/// JSON value returned by HTTP client.
using json = boost::json::value;

/// Single HTTP header (name, value).
using Header  = std::pair<std::string_view, std::string_view>;
/// Sequence of HTTP headers.
using Headers = std::span<const Header>;

/// A high-performance HTTP/1.1-over-TLS client with:
///  * persistent connection pooling (keep-alive)
///  * reusable Beast buffers
///  * minimal per-request allocations
class Client {
public:
    /// Construct on given executor and SSL context.
    Client(boost::asio::any_io_executor exec,
           boost::asio::ssl::context& ssl_ctx);

    /// Perform HTTP GET and return parsed JSON.
    [[nodiscard]]
    boost::asio::awaitable<json> get(
        std::string_view host,
        std::string_view port,
        std::string_view target,
        Headers headers = {});

    /// Perform HTTP POST with a string body and return parsed JSON.
    [[nodiscard]]
    boost::asio::awaitable<json> post(
        std::string_view host,
        std::string_view port,
        std::string_view target,
        std::string_view body,
        Headers headers = {});

private:
    struct Connection;

    boost::asio::any_io_executor                      executor_;
    boost::asio::ssl::context&                        ssl_ctx_;
    boost::asio::ip::tcp::resolver                    resolver_;
    boost::asio::strand<boost::asio::any_io_executor> strand_;

    // Idle connections keyed by "host:port"
    std::unordered_map<
        std::string,
        std::vector<std::shared_ptr<Connection>>,
        TransparentStringHash,
        TransparentStringEq
    > pool_;

    // Core implementation behind both GET and POST
    boost::asio::awaitable<json> perform(
        boost::beast::http::verb method,
        std::string_view host,
        std::string_view port,
        std::string_view target,
        std::string_view body,
        Headers headers);
};

} // namespace http_client
