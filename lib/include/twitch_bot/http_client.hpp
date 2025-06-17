#pragma once

// C++ Standard Library
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// 3rd-party
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/strand.hpp>

#include <boost/beast/http.hpp>

#include <glaze/json.hpp>

// Project
#include "utils/attributes.hpp"
#include "utils/transparent_string.hpp"

namespace http_client {

using json = glz::json_t;
using result = glz::expected<json, glz::error_ctx>;

/// Initial pool sizes.
inline constexpr std::size_t kDefaultExpectedHosts = 16;
inline constexpr std::size_t kDefaultConnectionsPerHost = 4;

/// Glaze options shared by every request.
inline constexpr glz::opts json_opts{.null_terminated = true,
                                     .error_on_unknown_keys = false,
                                     .minified = true};

using http_header = std::pair<std::string_view, std::string_view>;
using http_headers = std::span<const http_header>;

/// HTTP/1.1-over-TLS client with keep-alive and minimal allocations.
class client
{
public:
    using allocator_type = std::pmr::polymorphic_allocator<std::byte>;

    /// Construct with \p executor and \p ssl_context; pre-allocate pools.
    client(boost::asio::any_io_executor executor,
           boost::asio::ssl::context& ssl_context,
           std::size_t expected_hosts = kDefaultExpectedHosts,
           std::size_t expected_conns_per_host = kDefaultConnectionsPerHost) noexcept;

    allocator_type get_allocator() const noexcept;

    [[nodiscard]]
    boost::asio::awaitable<result> get(std::string_view host,
                                       std::string_view port,
                                       std::string_view target,
                                       http_headers headers = {}) noexcept;

    [[nodiscard]]
    boost::asio::awaitable<result> post(std::string_view host,
                                        std::string_view port,
                                        std::string_view target,
                                        std::string_view body,
                                        http_headers headers = {}) noexcept;

private:
    struct connection;

    [[nodiscard]]
    boost::asio::awaitable<result> perform(boost::beast::http::verb method,
                                           std::string_view host,
                                           std::string_view port,
                                           std::string_view target,
                                           std::string_view body,
                                           http_headers headers) noexcept;

    boost::asio::any_io_executor executor_;
    boost::asio::ssl::context* ssl_context_; ///< stored as pointer, avoids ref member
    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::strand<boost::asio::any_io_executor> strand_;

    std::unordered_map<std::string,
                       std::pmr::vector<std::shared_ptr<connection>>,
                       TransparentStringHash,
                       TransparentStringEq>
        pool_;

    /// Memory resource for small handler allocations.
    mutable std::pmr::monotonic_buffer_resource handler_buffer_;

    std::size_t expected_conns_per_host_; ///< mutable count for move support
};

} // namespace http_client
