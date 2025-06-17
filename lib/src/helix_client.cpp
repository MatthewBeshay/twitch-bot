// C++ Standard Library
#include <array>
#include <system_error>

// 3rd-party
#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/version.hpp>
#include <openssl/ssl.h>

// Project
#include "helix_client.hpp"
#include "utils/attributes.hpp"

namespace twitch_bot {
namespace detail {

    /// Form-urlencoded body for the OAuth2 token request.
    inline std::string make_token_body(std::string_view client_id,
                                       std::string_view client_secret) noexcept
    {
        std::string body;
        body.reserve(64);
        body = "client_id=";
        body += client_id;
        body += "&client_secret=";
        body += client_secret;
        body += "&grant_type=client_credentials";
        return body;
    }

    /// Parse \p n decimal digits starting at \p p.
    inline int parse_digits(const char* TB_RESTRICT p, int n) noexcept
    {
        int v = 0;
        for (int i = 0; i < n; ++i)
            v = v * 10 + (p[i] - '0');
        return v;
    }

    /// Days since 1970-01-01 (civil calendar).
    inline constexpr int64_t days_from_civil(int64_t y, unsigned m, unsigned d) noexcept
    {
        y -= m <= 2;
        const int64_t era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = static_cast<unsigned>(y - era * 400);
        const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + static_cast<int64_t>(doe) - 719468;
    }

} // namespace detail

HelixClient::HelixClient(boost::asio::any_io_executor executor,
                         boost::asio::ssl::context& ssl_ctx,
                         std::string_view client_id,
                         std::string_view client_secret) noexcept
    : executor_{executor}
    , http_client_{executor_, ssl_ctx}
    , client_id_{client_id}
    , client_secret_{client_secret}
    , token_expiry_{std::chrono::steady_clock::now()}
{
    token_body_ = detail::make_token_body(client_id_, client_secret_);
    target_path_.reserve(64);
    auth_header_.reserve(64);
}

boost::asio::awaitable<void> HelixClient::ensure_token() noexcept
{
    {
        std::shared_lock lock{token_mutex_};
        if (!token_.empty() && std::chrono::steady_clock::now() < token_expiry_)
            co_return;
    }

    static constexpr std::string_view host = "id.twitch.tv";
    static constexpr std::string_view port = "443";
    static constexpr std::string_view target = "/oauth2/token";

    static const http_client::http_header hdr{"Content-Type", "application/x-www-form-urlencoded"};
    http_client::http_headers headers{&hdr, 1};

    http_client::result res;
    try {
        res = co_await http_client_.post(host, port, target, token_body_, headers);
    } catch (...) {
        std::unique_lock lock{token_mutex_};
        token_.clear();
        co_return;
    }

    if (!res) {
        std::unique_lock lock{token_mutex_};
        token_.clear();
        co_return;
    }

    auto j = std::move(*res);
    try {
        auto tok = j["access_token"].get<std::string>();
        int secs = detail::parse_digits(j["expires_in"].get<std::string>().data(), 10);

        std::unique_lock lock{token_mutex_};
        token_ = std::move(tok);
        token_expiry_ = std::chrono::steady_clock::now() + std::chrono::seconds{secs};
    } catch (...) {
        std::unique_lock lock{token_mutex_};
        token_.clear();
    }
}

boost::asio::awaitable<std::optional<stream_start_result>>
HelixClient::get_stream_start(std::string_view channel_id) noexcept
{
    co_await ensure_token();

    {
        std::shared_lock lock{token_mutex_};
        if (token_.empty())
            co_return std::nullopt;
        auth_header_ = "Bearer ";
        auth_header_ += token_;
    }

    static constexpr std::string_view host = "api.twitch.tv";
    static constexpr std::string_view port = "443";
    static constexpr std::string_view prefix = "/helix/streams?user_login=";

    target_path_.assign(prefix);
    target_path_ += channel_id;

    std::array<http_client::http_header, 2> hdrs{
        {{"Client-ID", client_id_}, {"Authorization", auth_header_}}};
    http_client::http_headers headers{hdrs.data(), hdrs.size()};

    http_client::result res;
    try {
        res = co_await http_client_.get(host, port, target_path_, headers);
    } catch (...) {
        co_return std::nullopt;
    }

    if (!res)
        co_return std::nullopt;

    auto j = std::move(*res);
    try {
        auto& arr = j["data"].get<decltype(j)::array_t>();
        if (arr.empty())
            co_return std::nullopt;

        auto& first = arr.front();
        auto ts = first["started_at"].get<std::string>();
        auto ms = parse_iso8601_ms(ts);
        if (!ms)
            co_return std::nullopt;

        co_return stream_start_result{true, *ms};
    } catch (...) {
        co_return std::nullopt;
    }
}

std::optional<std::chrono::milliseconds> HelixClient::parse_iso8601_ms(std::string_view ts) noexcept
{
    // Expected: YYYY-MM-DDThh:mm:ssZ
    if (ts.size() != 20 || ts[4] != '-' || ts[7] != '-' || ts[10] != 'T' || ts[13] != ':'
        || ts[16] != ':' || ts[19] != 'Z')
        return std::nullopt;

    int y = detail::parse_digits(ts.data() + 0, 4);
    unsigned m = detail::parse_digits(ts.data() + 5, 2);
    unsigned d = detail::parse_digits(ts.data() + 8, 2);
    unsigned hh = detail::parse_digits(ts.data() + 11, 2);
    unsigned mm = detail::parse_digits(ts.data() + 14, 2);
    unsigned ss = detail::parse_digits(ts.data() + 17, 2);

    int64_t days = detail::days_from_civil(y, m, d);
    int64_t s = days * 86400LL + hh * 3600 + mm * 60 + ss;
    return std::chrono::milliseconds{s * 1000LL};
}

} // namespace twitch_bot
