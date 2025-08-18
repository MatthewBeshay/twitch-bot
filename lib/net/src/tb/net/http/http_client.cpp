// C++ standard library
#include <system_error>

// Boost.Asio
#include <boost/asio/connect.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/use_awaitable.hpp>

// Boost.Beast
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

// OpenSSL
#include <openssl/err.h>
#include <openssl/ssl.h>

// Glaze
#include <glaze/json.hpp>

// GSL
#include <gsl/gsl>

// Project
#include <tb/net/http/cookie.hpp>
#include <tb/net/http/cookie_jar.hpp>
#include <tb/net/http/encoding.hpp>
#include <tb/net/http/error.hpp>
#include <tb/net/http/http_client.hpp>
#include <tb/net/http/redirect_policy.hpp>
#include <tb/net/http/url.hpp>

namespace http_client {

client::client(boost::asio::any_io_executor executor,
               boost::asio::ssl::context& ssl_context,
               std::size_t expected_hosts,
               std::size_t expected_conns_per_host) noexcept
    : executor_{executor}
    , ssl_context_{&ssl_context}
    , resolver_{executor_}
    , strand_{executor_}
    , expected_conns_per_host_{expected_conns_per_host}
{
    Expects(ssl_context_ != nullptr);
    pool_.reserve(expected_hosts);
}

auto client::get_allocator() const noexcept -> allocator_type
{
    return allocator_type(&handler_buffer_);
}

void client::shutdown() noexcept
{
    for (auto& [key, vec] : pool_) {
        for (auto& c : vec) {
            if (!c)
                continue;
            try {
                // Best effort: shutdown TLS and close
                boost::beast::get_lowest_layer(c->stream).socket().cancel();
                c->stream.shutdown();
            } catch (...) {
            }
            try {
                boost::beast::get_lowest_layer(c->stream).socket().close();
            } catch (...) {
            }
        }
        vec.clear();
    }
    pool_.clear();
}

static inline std::string default_port_for_scheme(std::string_view scheme)
{
    if (scheme == "https") {
        return "443";
    }
    if (scheme == "http") {
        return "80";
    }
    return {};
}

auto client::perform(boost::beast::http::verb method,
                     std::string_view host,
                     std::string_view port,
                     std::string_view target,
                     std::string_view body,
                     http_headers headers,
                     const RequestOptions* opts,
                     RequestMetrics* out_metrics) -> boost::asio::awaitable<result>
{
    namespace asio = boost::asio;
    namespace beast = boost::beast;
    namespace http = beast::http;

    auto tok
        = asio::bind_allocator(get_allocator(), asio::bind_executor(strand_, asio::use_awaitable));

    std::string cur_host{host};
    std::string cur_port{port.empty() ? std::string(detail::default_port_for_scheme("https"))
                                      : std::string(port)};
    std::string cur_target{target};

    tb::net::Url base;
    base.scheme = "https";

    RequestMetrics metrics{};
    metrics.method = method;

    const auto start_total = std::chrono::steady_clock::now();

    for (std::size_t hop = 0; hop <= redirect_policy_.max_hops(); ++hop) {
        metrics.host = cur_host;
        metrics.port = cur_port;
        metrics.target = cur_target;

        std::string key = detail::make_pool_key(cur_host, cur_port);
        auto pool_it = pool_.find(key);
        std::shared_ptr<connection> conn;

        if (pool_it != pool_.end() && !pool_it->second.empty()) {
            conn = std::move(pool_it->second.back());
            pool_it->second.pop_back();
            metrics.reused_connection = true;
        } else {
            metrics.reused_connection = false;
        }

        if (conn && std::chrono::steady_clock::now() - conn->last_used > k_pool_idle_timeout) {
            conn.reset();
        }

        if (!conn) {
            if (pool_it == pool_.end()) {
                auto& v = pool_[key];
                v.reserve(expected_conns_per_host_);
                pool_it = pool_.find(key);
            }

            const auto t_dns_start = std::chrono::steady_clock::now();
            auto endpoints = co_await resolver_.async_resolve(cur_host, cur_port, tok);
            metrics.t_dns = std::chrono::steady_clock::now() - t_dns_start;

            beast::tcp_stream tcp(executor_);
            beast::get_lowest_layer(tcp).expires_after(
                or_default(opts ? opts->tcp_connect_timeout : std::chrono::steady_clock::duration{},
                           k_tcp_connect_timeout));
            const auto t_conn_start = std::chrono::steady_clock::now();
            co_await tcp.async_connect(endpoints, tok);
            metrics.t_connect = std::chrono::steady_clock::now() - t_conn_start;

            beast::get_lowest_layer(tcp).socket().set_option(asio::ip::tcp::no_delay{true});

            typename connection::ssl_stream ssl{std::move(tcp), *ssl_context_};
            if (!::SSL_set_tlsext_host_name(ssl.native_handle(), cur_host.c_str())) {
                throw std::system_error{static_cast<int>(::ERR_get_error()),
                                        asio::error::get_ssl_category(), "SNI failure"};
            }

            beast::get_lowest_layer(ssl).expires_after(or_default(
                opts ? opts->tls_handshake_timeout : std::chrono::steady_clock::duration{},
                k_handshake_timeout));
            const auto t_tls_start = std::chrono::steady_clock::now();
            co_await ssl.async_handshake(asio::ssl::stream_base::client, tok);
            metrics.t_tls = std::chrono::steady_clock::now() - t_tls_start;

            conn = std::make_shared<connection>(std::move(ssl));
        }

        auto& vec = pool_[key];
        bool keep_alive = true;
        auto return_to_pool = gsl::finally([&] {
            if (keep_alive && conn
                && boost::beast::get_lowest_layer(conn->stream).socket().is_open()
                && vec.size() < expected_conns_per_host_) {
                conn->mark_used();
                vec.push_back(std::move(conn));
            }
        });

        const std::string host_hdr
            = detail::host_header_value(cur_host, cur_port, /*scheme*/ "https");
        http::request<http::string_body> req{method, cur_target, k_http_version};
        req.set(http::field::host, host_hdr);
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        if (!opts || opts->accept.empty()) {
            req.set(http::field::accept, "application/json");
        } else {
            req.set(http::field::accept, opts->accept);
        }
#if defined(USE_BROTLI)
        if (!opts || opts->accept_encoding.empty()) {
            req.set(http::field::accept_encoding, "gzip, br");
        } else {
            req.set(http::field::accept_encoding, opts->accept_encoding);
        }
#else
        if (!opts || opts->accept_encoding.empty()) {
            req.set(http::field::accept_encoding, "gzip");
        } else {
            req.set(http::field::accept_encoding, opts->accept_encoding);
        }
#endif

        if (cookies_enabled_) {
            auto path = detail::path_from_target(cur_target);
            auto cookie_line = cookies_.cookie_header_for(cur_host, path, /*is_https*/ true);
            if (!cookie_line.empty())
                req.set(http::field::cookie, cookie_line);
        }

        if (method == http::verb::post) {
            req.body() = std::string{body};
            req.prepare_payload();
        }
        for (auto& h : headers) {
            req.set(h.first, h.second);
        }

        conn->reset();
        boost::beast::get_lowest_layer(conn->stream)
            .expires_after(
                or_default(opts ? opts->write_timeout : std::chrono::steady_clock::duration{},
                           k_http_write_timeout));
        const auto t_write_start = std::chrono::steady_clock::now();
        co_await http::async_write(conn->stream, req, tok);
        metrics.t_write = std::chrono::steady_clock::now() - t_write_start;

        boost::beast::get_lowest_layer(conn->stream)
            .expires_after(
                or_default(opts ? opts->read_timeout : std::chrono::steady_clock::duration{},
                           k_http_read_timeout));
        http::response<http::string_body> res;
        const auto t_ttfb_start = std::chrono::steady_clock::now();
        co_await http::async_read(conn->stream, conn->buffer, res, tok);
        metrics.t_ttfb = std::chrono::steady_clock::now() - t_ttfb_start;

        keep_alive = res.keep_alive();
        metrics.status = res.result_int();

        if (cookies_enabled_) {
            auto path = detail::path_from_target(cur_target);
            for (const auto& f : res.base()) {
                if (f.name() == http::field::set_cookie) {
                    cookies_.store_from_set_cookie(
                        std::string_view{f.value().data(), f.value().size()}, cur_host, path,
                        /*from_https*/ true);
                }
            }
            cookies_.evict_expired();
        }

        const int status = metrics.status;

        if (tb::net::is_redirect_status(status)) {
            auto locIt = res.find(http::field::location);
            if (locIt == res.end()) {
                throw std::runtime_error("Redirect response missing Location header");
            }

            const auto tgt = tb::net::parse_url(cur_target);
            base.host = cur_host;
            base.port = cur_port;
            base.path = tgt.path;
            base.query = tgt.query;

            const std::string_view loc_sv{locIt->value().data(), locIt->value().size()};
            tb::net::Url to = tb::net::resolve_url(base, loc_sv);

            if (to.port.empty()) {
                to.port = std::string(detail::default_port_for_scheme(to.scheme));
            }

            auto next_method = tb::net::RedirectPolicy::next_verb(method, status);

            if (!redirect_policy_.allow_hop(base, to, next_method)) {
                throw std::runtime_error("Redirect not allowed by policy");
            }

            if (!to.scheme.empty() && to.scheme != "https") {
                throw std::runtime_error("Redirect to non-https is not supported");
            }

            method = next_method;
            cur_host = std::move(to.host);
            cur_port = to.port.empty() ? std::string(detail::default_port_for_scheme("https"))
                                       : std::move(to.port);
            cur_target = to.target();

            continue;
        }

        if (status < 200 || status >= 300) {
            throw std::runtime_error(detail::make_error_msg(cur_host, cur_target, status));
        }

        std::string body_decoded;
        if (!opts || !opts->disable_auto_decode) {
            tb::net::encoding::enc enc = tb::net::encoding::enc::none;
            if (auto itCE = res.find(http::field::content_encoding); itCE != res.end()) {
                enc = tb::net::encoding::parse_content_encoding(
                    std::string_view{itCE->value().data(), itCE->value().size()});
            }
            std::error_code dec_ec;
            if (!tb::net::encoding::decode(std::string_view{res.body().data(), res.body().size()},
                                           enc, body_decoded, dec_ec)) {
                throw std::system_error(dec_ec);
            }
        } else {
            body_decoded.assign(res.body().data(), res.body().size());
        }

        metrics.t_read = std::chrono::steady_clock::now() - (t_ttfb_start + metrics.t_ttfb);
        metrics.t_total = std::chrono::steady_clock::now() - start_total;

        if (metrics_cb_) {
            try {
                metrics_cb_(metrics);
            } catch (...) {
            }
        }

        body_decoded.push_back('\0');
        std::string_view sv{body_decoded.data(), body_decoded.size() - 1};

        json j{};
        if (glz::error_ctx ec = glz::read<json_opts>(j, sv); ec) {
            co_return glz::unexpected(ec);
        }
        co_return j;
    }

    throw std::runtime_error("Too many redirects");
}

auto client::get(std::string_view host,
                 std::string_view port,
                 std::string_view target,
                 http_headers headers) -> boost::asio::awaitable<result>
{
    co_return co_await perform(boost::beast::http::verb::get, host, port, target, {}, headers,
                               nullptr, nullptr);
}

auto client::post(std::string_view host,
                  std::string_view port,
                  std::string_view target,
                  std::string_view body,
                  http_headers headers) -> boost::asio::awaitable<result>
{
    co_return co_await perform(boost::beast::http::verb::post, host, port, target, body, headers,
                               nullptr, nullptr);
}

auto client::get_with_opts(std::string_view host,
                           std::string_view port,
                           std::string_view target,
                           http_headers headers,
                           const RequestOptions& opts) -> boost::asio::awaitable<result>
{
    co_return co_await perform(boost::beast::http::verb::get, host, port, target, {}, headers,
                               &opts, nullptr);
}

auto client::post_with_opts(std::string_view host,
                            std::string_view port,
                            std::string_view target,
                            std::string_view body,
                            http_headers headers,
                            const RequestOptions& opts) -> boost::asio::awaitable<result>
{
    co_return co_await perform(boost::beast::http::verb::post, host, port, target, body, headers,
                               &opts, nullptr);
}

auto client::get_with_retry(std::string_view host,
                            std::string_view port,
                            std::string_view target,
                            http_headers headers,
                            const RetryOptions& retry_opts,
                            const RequestOptions* opts) -> boost::asio::awaitable<result>
{
    namespace asio = boost::asio;

    for (int attempt = 1; attempt <= retry_opts.max_attempts; ++attempt) {
        std::optional<std::chrono::milliseconds> delay_to_apply;

        try {
            co_return co_await perform(boost::beast::http::verb::get, host, port, target, {},
                                       headers, opts, nullptr);
        } catch (const std::system_error& /*se*/) {
            if (retry_opts.retry_on_network_error && attempt < retry_opts.max_attempts) {
                delay_to_apply = retry_opts.next_delay(attempt);
            } else {
                throw;
            }
        } catch (const std::runtime_error& re) {
            const std::string_view msg{re.what()};
            const bool is_5xx = (msg.find(" returned 5") != std::string_view::npos);
            if (retry_opts.retry_on_5xx && is_5xx && attempt < retry_opts.max_attempts) {
                delay_to_apply = retry_opts.next_delay(attempt);
            } else {
                throw;
            }
        }

        if (delay_to_apply) {
            asio::steady_timer t(executor_);
            t.expires_after(*delay_to_apply);
            co_await t.async_wait(asio::bind_executor(strand_, asio::use_awaitable));
            continue;
        }
    }

    throw std::runtime_error("retry logic fell through");
}

} // namespace http_client
