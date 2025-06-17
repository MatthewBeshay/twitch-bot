// C++ Standard Library
#include <chrono>
#include <system_error>

// 3rd-party
#include <boost/asio/connect.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>

#include <openssl/ssl.h>

// Project
#include "http_client.hpp"
#include "utils/attributes.hpp"

namespace {

// Constants for time-outs, status ranges and buffer sizing.
inline constexpr std::size_t kErrorMsgReserveExtra = 32;
inline constexpr int kHttpStatusMin = 200;
inline constexpr int kHttpStatusMaxExcl = 300;
inline constexpr int kHttpVersion = 11;
inline constexpr auto kTcpConnectTimeout = std::chrono::seconds{30};
inline constexpr std::size_t kBufferSizeKilobytes = 16;

} // unnamed namespace

namespace http_client {
namespace detail {

    inline std::string make_pool_key(std::string_view host, std::string_view port) noexcept
    {
        std::string key;
        key.reserve(host.size() + 1 + port.size());

        key.append(host);
        key.push_back(':');
        key.append(port);

        return key;
    }

    inline std::string
    make_error_msg(std::string_view host, std::string_view target, int status) noexcept
    {
        std::string msg;
        msg.reserve(host.size() + target.size() + kErrorMsgReserveExtra);
        msg.append(host).append(target).append(" returned ").append(std::to_string(status));
        return msg;
    }

} // namespace detail

struct client::connection {
    boost::beast::ssl_stream<boost::beast::tcp_stream> stream;
    static constexpr std::size_t buffer_size = kBufferSizeKilobytes * 1024U;
    boost::beast::flat_static_buffer<buffer_size> buffer;

    explicit connection(boost::beast::ssl_stream<boost::beast::tcp_stream> s) noexcept
        : stream(std::move(s)), buffer()
    {
    }

    TB_FORCE_INLINE void reset() noexcept
    {
        buffer.clear();
    }
};

client::client(boost::asio::any_io_executor executor,
               boost::asio::ssl::context& ssl_context,
               std::size_t expected_hosts,
               std::size_t expected_conns_per_host) noexcept
    : executor_{executor}
    , ssl_context_{&ssl_context}
    , resolver_{executor_}
    , strand_{executor_}
    , pool_{}
    , expected_conns_per_host_{expected_conns_per_host}
{
    pool_.reserve(expected_hosts);
}

auto client::get_allocator() const noexcept -> allocator_type
{
    return allocator_type(&handler_buffer_);
}

auto client::perform(boost::beast::http::verb method,
                     std::string_view host,
                     std::string_view port,
                     std::string_view target,
                     std::string_view body,
                     http_headers headers) noexcept -> boost::asio::awaitable<result>
{
    co_await boost::asio::dispatch(strand_, boost::asio::use_awaitable);

    auto key = detail::make_pool_key(host, port);
    auto it = pool_.find(key);
    std::shared_ptr<connection> conn;

    if (it != pool_.end() && !it->second.empty()) {
        conn = std::move(it->second.back());
        it->second.pop_back();
    }

    if (!conn) {
        if (it == pool_.end()) {
            auto& vec = pool_[key];
            vec.reserve(expected_conns_per_host_);
            it = pool_.find(key);
        }

        auto endpoints = co_await resolver_.async_resolve(host, port, boost::asio::use_awaitable);

        boost::beast::tcp_stream tcp(executor_);
        tcp.expires_after(kTcpConnectTimeout);
        co_await tcp.async_connect(endpoints, boost::asio::use_awaitable);

        boost::beast::ssl_stream<boost::beast::tcp_stream> ssl(std::move(tcp), *ssl_context_);
        if (!SSL_set_tlsext_host_name(ssl.native_handle(), host.data()))
            throw std::system_error(static_cast<int>(::ERR_get_error()),
                                    boost::asio::error::get_ssl_category(),
                                    "SNI failure");

        co_await ssl.async_handshake(boost::asio::ssl::stream_base::client,
                                     boost::asio::use_awaitable);

        conn = std::make_shared<connection>(std::move(ssl));
    }

    boost::beast::http::request<boost::beast::http::string_body> req{method, target, kHttpVersion};
    req.set(boost::beast::http::field::host, host);
    req.set(boost::beast::http::field::user_agent, BOOST_BEAST_VERSION_STRING);

    for (auto& h : headers)
        req.set(h.first, h.second);

    if (method == boost::beast::http::verb::post) {
        req.body() = body;
        req.prepare_payload();
    }

    conn->reset();

    co_await boost::beast::http::async_write(conn->stream, req, boost::asio::use_awaitable);

    boost::beast::http::response<boost::beast::http::string_body> res;
    co_await boost::beast::http::async_read(conn->stream, conn->buffer, res,
                                            boost::asio::use_awaitable);

    int status = res.result_int();
    if (status < kHttpStatusMin || status >= kHttpStatusMaxExcl) {
        it->second.push_back(std::move(conn));
        throw std::runtime_error(detail::make_error_msg(host, target, status));
    }

    std::string body_buf = std::move(res.body());
    json j;
    auto ec = glz::read<json_opts>(j, body_buf);
    if (ec) {
        it->second.push_back(std::move(conn));
        co_return glz::unexpected(ec);
    }

    it->second.push_back(std::move(conn));
    co_return j;
}

auto client::get(std::string_view host,
                 std::string_view port,
                 std::string_view target,
                 http_headers headers) noexcept -> boost::asio::awaitable<result>
{
    co_return co_await perform(boost::beast::http::verb::get, host, port, target, {}, headers);
}

auto client::post(std::string_view host,
                  std::string_view port,
                  std::string_view target,
                  std::string_view body,
                  http_headers headers) noexcept -> boost::asio::awaitable<result>
{
    co_return co_await perform(boost::beast::http::verb::post, host, port, target, body, headers);
}

} // namespace http_client
