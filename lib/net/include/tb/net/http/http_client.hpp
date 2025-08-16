#pragma once

// C++ standard library
#include <chrono>
#include <functional>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Boost.Asio
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/bind_allocator.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>

// Boost.Beast
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>

// OpenSSL
#include <openssl/err.h>
#include <openssl/ssl.h>

// Glaze
#include <glaze/json.hpp>

// GSL
#include <gsl/gsl>

// Project
#include "chunked_encoding.hpp"
#include "cookie.hpp"
#include "cookie_jar.hpp"
#include "redirect_policy.hpp"
#include "url.hpp"
#include <tb/utils/attributes.hpp>
#include <tb/utils/transparent_string_hash.hpp>

namespace http_client {

namespace detail {

    // Build a key for the connection-pool map.
    inline std::string make_pool_key(std::string_view host, std::string_view port) noexcept {
        std::string key;
        key.reserve(host.size() + 1 + port.size());
        key.append(host);
        key.push_back(':');
        key.append(port);
        return key;
    }

    // Build an error message with host, target and status code.
    inline std::string make_error_msg(std::string_view host,
                                      std::string_view target,
                                      int               status) noexcept {
        std::string msg;
        msg.reserve(host.size() + target.size() + 32);
        msg.append(host).append(target).append(" returned ").append(std::to_string(status));
        return msg;
    }

    // Extract just the path from an HTTP target (e.g. "/p?q#f" -> "/p")
    inline std::string_view path_from_target(std::string_view target) noexcept {
        if (target.empty()) return std::string_view{"/"};
        const auto q = target.find('?');
        auto path    = (q == std::string_view::npos) ? target : target.substr(0, q);
        if (path.empty()) return std::string_view{"/"};
        return path;
    }

    // Default port string for a scheme
    inline std::string_view default_port_for_scheme(std::string_view scheme) noexcept {
        if (scheme == "https") return "443";
        if (scheme == "http") return "80";
        return {};
    }

    // Produce Host header value, appending :port when port is non-default for the scheme
    inline std::string host_header_value(std::string_view host,
                                         std::string_view port,
                                         std::string_view scheme) {
        const auto def = default_port_for_scheme(scheme);
        if (!port.empty() && port != def) {
            std::string v;
            v.reserve(host.size() + 1 + port.size());
            v.append(host);
            v.push_back(':');
            v.append(port);
            return v;
        }
        return std::string(host);
    }

} // namespace detail

// JSON options visible to both header and source
inline constexpr glz::opts json_opts{
    .null_terminated         = true,
    .error_on_unknown_keys   = false,
    .minified                = true,
};

// Type aliases for JSON values and results
using json   = glz::json_t;
using result = glz::expected<json, glz::error_ctx>;

// HTTP header types
using http_header  = std::pair<std::string_view, std::string_view>;
using http_headers = std::span<const http_header>;

// Default pool sizes
inline constexpr std::size_t k_default_expected_hosts      = 16;
inline constexpr std::size_t k_default_connections_per_host = 4;

// HTTP and pooling constants
inline constexpr int   k_http_version          = 11;
inline constexpr auto  k_tcp_connect_timeout   = std::chrono::seconds{30};
inline constexpr auto  k_handshake_timeout     = std::chrono::seconds{10};
inline constexpr auto  k_http_write_timeout    = std::chrono::seconds{10};
inline constexpr auto  k_http_read_timeout     = std::chrono::seconds{10};
inline constexpr auto  k_pool_idle_timeout     = std::chrono::seconds{60};
inline constexpr std::size_t k_buffer_size_kb  = 16;

class client
{
public:
    using allocator_type = std::pmr::polymorphic_allocator<std::byte>;

    // ------------ telemetry / options / retry ------------

    struct RequestMetrics {
        boost::beast::http::verb method{boost::beast::http::verb::get};
        std::string host, port, target;
        int         status{0};

        // timings
        std::chrono::steady_clock::duration t_dns{};
        std::chrono::steady_clock::duration t_connect{};
        std::chrono::steady_clock::duration t_tls{};
        std::chrono::steady_clock::duration t_write{};
        std::chrono::steady_clock::duration t_ttfb{}; // time to first byte (headers)
        std::chrono::steady_clock::duration t_read{};
        std::chrono::steady_clock::duration t_total{};

        bool reused_connection{false};
    };

    using MetricsCallback = std::function<void(const RequestMetrics&)>;

    struct RequestOptions {
        // Per-request timeout overrides; 0 => use client defaults
        std::chrono::steady_clock::duration tcp_connect_timeout{};
        std::chrono::steady_clock::duration tls_handshake_timeout{};
        std::chrono::steady_clock::duration write_timeout{};
        std::chrono::steady_clock::duration read_timeout{};

        // Force specific Accept / Accept-Encoding (leave empty to use defaults)
        std::string accept;
        std::string accept_encoding;

        // If true, skip automatic content decoding (you’ll get raw body)
        bool disable_auto_decode{false};
    };

    struct RetryOptions {
        int max_attempts{3}; // total attempts (1 initial + 2 retries)
        bool retry_on_network_error{true};
        bool retry_on_5xx{true};
        std::chrono::milliseconds initial_delay{200};
        double backoff_factor{2.0};
        std::chrono::milliseconds max_delay{2000};

        // simple helper
        std::chrono::milliseconds next_delay(int attempt) const {
            using dbl = std::chrono::duration<double,std::milli>;
            auto d = dbl(initial_delay).count() * std::pow(backoff_factor, attempt - 1);
            auto ms = static_cast<long long>(d + 0.5);
            return std::min(std::chrono::milliseconds{ms}, max_delay);
        }
    };

    // ------------ construction ------------

    client(boost::asio::any_io_executor executor,
           boost::asio::ssl::context&   ssl_context,
           std::size_t                  expected_hosts = k_default_expected_hosts,
           std::size_t expected_conns_per_host        = k_default_connections_per_host) noexcept;

    [[nodiscard]] allocator_type get_allocator() const noexcept;

    // ------------ basic API (unchanged signatures) ------------

    [[nodiscard]]
    boost::asio::awaitable<result> get(std::string_view host,
                                       std::string_view port,
                                       std::string_view target,
                                       http_headers headers = {});

    [[nodiscard]]
    boost::asio::awaitable<result> post(std::string_view host,
                                        std::string_view port,
                                        std::string_view target,
                                        std::string_view body,
                                        http_headers headers = {});

    // ------------ new: per-request options + retry ------------

    [[nodiscard]]
    boost::asio::awaitable<result> get_with_opts(std::string_view host,
                                                 std::string_view port,
                                                 std::string_view target,
                                                 http_headers headers,
                                                 const RequestOptions& opts);

    [[nodiscard]]
    boost::asio::awaitable<result> post_with_opts(std::string_view host,
                                                  std::string_view port,
                                                  std::string_view target,
                                                  std::string_view body,
                                                  http_headers headers,
                                                  const RequestOptions& opts);

    [[nodiscard]]
    boost::asio::awaitable<result> get_with_retry(std::string_view host,
                                                  std::string_view port,
                                                  std::string_view target,
                                                  http_headers headers,
                                                  const RetryOptions& retry_opts,
                                                  const RequestOptions* opts = nullptr);

    // ------------ chunked streaming (identity only) ------------

    /// Decode a chunked GET stream. Handler is called for each chunk.
    /// NOTE: this helper forces Accept-Encoding: identity and will throw if the
    /// server returns a compressed body or non-2xx status.
    template <typename ChunkHandler>
    boost::asio::awaitable<void> stream_get(std::string_view host,
                                            std::string_view port,
                                            std::string_view target,
                                            http_headers headers,
                                            ChunkHandler handler);

    // ------------ cookies & redirects ------------

    void enable_cookies(bool on = true) noexcept { cookies_enabled_ = on; }
    void clear_cookies() noexcept { cookies_.clear(); }
    void add_cookie(const tb::net::Cookie& c,
                    std::string_view host,
                    std::string_view path = "/",
                    bool from_https = true)
    {
        if (!cookies_enabled_) return;
        cookies_.store(c, host, path, from_https);
    }

    void set_redirect_policy(tb::net::RedirectPolicy p) noexcept { redirect_policy_ = p; }
    [[nodiscard]] tb::net::RedirectPolicy redirect_policy() const noexcept { return redirect_policy_; }

    // ------------ telemetry & shutdown ------------

    void set_metrics_callback(MetricsCallback cb) { metrics_cb_ = std::move(cb); }

    /// Close all pooled connections (e.g., on shutdown).
    void shutdown() noexcept;

private:
    struct connection {
        using tcp_stream = boost::beast::tcp_stream;
        using ssl_stream = boost::beast::ssl_stream<tcp_stream>;

        ssl_stream                                          stream;
        static constexpr std::size_t buffer_size = k_buffer_size_kb * 1024U;
        boost::beast::flat_static_buffer<buffer_size>       buffer;
        std::chrono::steady_clock::time_point               last_used;

        explicit connection(ssl_stream s) noexcept
            : stream(std::move(s)), buffer(), last_used(std::chrono::steady_clock::now()) {}

        void reset() noexcept { buffer.clear(); }
        void mark_used() noexcept { last_used = std::chrono::steady_clock::now(); }
    };

    [[nodiscard]]
    boost::asio::awaitable<result> perform(boost::beast::http::verb method,
                                           std::string_view host,
                                           std::string_view port,
                                           std::string_view target,
                                           std::string_view body,
                                           http_headers headers,
                                           const RequestOptions* opts,
                                           RequestMetrics* out_metrics);

    // defaulted timeouts if per-request options are absent
    static inline std::chrono::steady_clock::duration or_default(
        std::chrono::steady_clock::duration v,
        std::chrono::steady_clock::duration def) noexcept
    {
        return (v == std::chrono::steady_clock::duration::zero()) ? def : v;
    }

    boost::asio::any_io_executor                               executor_;
    boost::asio::ssl::context*                                 ssl_context_; // non-null
    boost::asio::ip::tcp::resolver                             resolver_;
    boost::asio::strand<boost::asio::any_io_executor>          strand_;

    // allocator used for handler memory in bind_allocator
    mutable std::pmr::monotonic_buffer_resource                handler_buffer_;

    // hetero-lookup pool keyed by "host:port"
    std::unordered_map<std::string,
                       std::pmr::vector<std::shared_ptr<connection>>,
                       TransparentBasicStringHash<char>,
                       TransparentBasicStringEq<char>>
        pool_;

    std::size_t                  expected_conns_per_host_{};

    // Cookies
    bool                         cookies_enabled_{true};
    tb::net::CookieJar           cookies_;

    // Redirects
    tb::net::RedirectPolicy      redirect_policy_{}; // default: follow GET/HEAD up to N hops

    // Telemetry
    MetricsCallback              metrics_cb_{};
};

// ------------------- stream_get template implementation -------------------

template <typename ChunkHandler>
boost::asio::awaitable<void> client::stream_get(std::string_view host,
                                                std::string_view port,
                                                std::string_view target,
                                                http_headers     headers,
                                                ChunkHandler     handler)
{
    namespace asio = boost::asio;
    namespace beast = boost::beast;
    namespace http = beast::http;

    auto tok = asio::bind_allocator(get_allocator(),
               asio::bind_executor(strand_, asio::use_awaitable));

    // Build pool key and obtain or create connection vector
    std::string key = detail::make_pool_key(host, port);
    auto [it, inserted]
        = pool_.try_emplace(key, std::pmr::vector<std::shared_ptr<connection>>(&handler_buffer_));
    auto& vec = it->second;
    if (inserted) vec.reserve(expected_conns_per_host_);

    std::shared_ptr<connection> conn;
    if (!vec.empty()) { conn = std::move(vec.back()); vec.pop_back(); }

    // Drop idle connections
    if (conn && std::chrono::steady_clock::now() - conn->last_used > k_pool_idle_timeout) {
        conn.reset();
    }

    if (!conn) {
        // Resolve DNS
        auto endpoints = co_await resolver_.async_resolve(host, port, tok);

        // Establish TCP connection
        beast::tcp_stream tcp(executor_);
        beast::get_lowest_layer(tcp).expires_after(k_tcp_connect_timeout);
        co_await tcp.async_connect(endpoints, tok);

        // Disable Nagle's algorithm
        beast::get_lowest_layer(tcp).socket().set_option(asio::ip::tcp::no_delay{true});

        // Upgrade to TLS
        typename connection::ssl_stream ssl{std::move(tcp), *ssl_context_};
        const std::string host_sni{host}; // NUL-terminated
        if (!::SSL_set_tlsext_host_name(ssl.native_handle(), host_sni.c_str())) {
            throw std::system_error{static_cast<int>(::ERR_get_error()),
                                    asio::error::get_ssl_category(), "SNI failure"};
        }
        beast::get_lowest_layer(ssl).expires_after(k_handshake_timeout);
        co_await ssl.async_handshake(asio::ssl::stream_base::client, tok);

        conn = std::make_shared<connection>(std::move(ssl));
    }

    // Send GET request (force identity; streaming decompression not supported here)
    conn->reset();
    const std::string host_hdr = detail::host_header_value(host, port, /*scheme*/"https");
    http::request<http::empty_body> req{http::verb::get, target, k_http_version};
    req.set(http::field::host, host_hdr);
    req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
    req.set(http::field::accept, "application/json");
    req.set(http::field::accept_encoding, "identity"); // important

    // Attach Cookie header if available
    if (cookies_enabled_) {
        auto path = detail::path_from_target(target);
        auto cookie_line = cookies_.cookie_header_for(host, path, /*is_https*/true);
        if (!cookie_line.empty()) req.set(http::field::cookie, cookie_line);
    }

    for (auto& h : headers) req.set(h.first, h.second);

    beast::get_lowest_layer(conn->stream).expires_after(k_http_write_timeout);
    co_await http::async_write(conn->stream, req, tok);

    // Read response headers
    http::response_parser<http::empty_body> parser;
    beast::get_lowest_layer(conn->stream).expires_after(k_http_read_timeout);
    co_await http::async_read_header(conn->stream, conn->buffer, parser, tok);
    auto& resp = parser.get();

    const int  status     = resp.result_int();
    const bool is_chunked = resp.chunked();
    const bool keep_alive = resp.keep_alive();

    // Fail fast on non-2xx
    if (status < 200 || status >= 300) {
        throw std::runtime_error(detail::make_error_msg(host, target, status));
    }
    // No compressed streaming in this helper
    if (auto itCE = resp.base().find(http::field::content_encoding);
        itCE != resp.base().end())
    {
        throw std::runtime_error("stream_get: compressed responses are not supported");
    }

    // Capture Set-Cookie from headers
    if (cookies_enabled_) {
        auto path = detail::path_from_target(target);
        for (auto const& f : resp.base()) {
            if (f.name() == http::field::set_cookie) {
                cookies_.store_from_set_cookie(
                    std::string_view{f.value().data(), f.value().size()},
                    host, path, /*from_https*/true);
            }
        }
        cookies_.evict_expired();
    }

    // RAII: ensure the connection goes back to pool on all paths (only if keep-alive)
    auto return_to_pool = gsl::finally([&, keep_alive] {
        if (keep_alive && conn && beast::get_lowest_layer(conn->stream).socket().is_open()
            && vec.size() < expected_conns_per_host_) {
            conn->mark_used();
            vec.push_back(std::move(conn));
        }
    });

    if (is_chunked) {
        std::uint64_t chunk_state = 0;

        // process whatever is currently readable in the buffer
        auto process_buffer = [&](auto& cbuf) -> bool {
            // return true when finished (body + trailer consumed)
            auto  seq = cbuf.data();
            if (boost::asio::buffer_size(seq) == 0) return false;

            // flat_static_buffer is contiguous; read from the front
            auto        first = beast::buffers_front(seq);
            const char* begin = static_cast<const char*>(first.data());
            const char* p     = begin;
            std::size_t avail = first.size();

            for (;;) {
                if (avail == 0) break;
                if (auto sv = get_next_chunk(p, avail, chunk_state)) {
                    const bool fin = !is_parsing_chunked_encoding(chunk_state);
                    handler(*sv, fin);
                    if (fin) {
                        const std::size_t consumed = static_cast<std::size_t>(p - begin);
                        cbuf.consume(consumed);
                        return true; // all done
                    }
                } else {
                    break; // need more bytes
                }
            }

            // consume processed bytes, keep leftovers for next read
            const std::size_t consumed = static_cast<std::size_t>(p - begin);
            cbuf.consume(consumed);
            return false;
        };

        if (process_buffer(conn->buffer)) co_return;

        // keep reading until decoder signals done
        for (;;) {
            const std::size_t cap     = connection::buffer_size - conn->buffer.size();
            const std::size_t to_read = (std::min<std::size_t>)(8192, cap);

            beast::get_lowest_layer(conn->stream).expires_after(k_http_read_timeout);
            const std::size_t n
                = co_await conn->stream.async_read_some(conn->buffer.prepare(to_read), tok);
            conn->buffer.commit(n);

            if (process_buffer(conn->buffer)) co_return;

            // If no forward progress is possible, you could throw here for malformed input.
        }
    } else {
        // Non-chunked - finish and deliver whole body
        http::response<http::string_body> full;
        beast::get_lowest_layer(conn->stream).expires_after(k_http_read_timeout);
        co_await http::async_read(conn->stream, conn->buffer, full, tok);

        // Defensive: absorb Set-Cookie from the full response too
        if (cookies_enabled_) {
            auto path = detail::path_from_target(target);
            for (auto const& f : full.base()) {
                if (f.name() == http::field::set_cookie) {
                    cookies_.store_from_set_cookie(
                        std::string_view{f.value().data(), f.value().size()},
                        host, path, /*from_https*/true);
                }
            }
            cookies_.evict_expired();
        }

        handler(full.body(), true);
    }
}

} // namespace http_client
