#include "helix_client.hpp"

#include <array>
#include <chrono>
#include <system_error>

#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/version.hpp>
#include <openssl/ssl.h>

namespace twitch_bot {
namespace detail {

/// Build the URL-encoded body for the OAuth client_credentials flow.
inline std::string makeTokenRequestBody(std::string_view client_id,
                                        std::string_view client_secret) noexcept 
{
    std::string body;
    // "client_id=<cid>&client_secret=<secret>&grant_type=client_credentials"
    body.reserve(10 + client_id.size() + 15 + client_secret.size() + 30);
    body.append("client_id=").append(client_id)
        .append("&client_secret=").append(client_secret)
        .append("&grant_type=client_credentials");
    return body;
}

/// Parse exactly `n` decimal digits from `p` (no validation).
inline int parseDigits(const char* p, int n) noexcept {
    int v = 0;
    for (int i = 0; i < n; ++i) {
        v = v * 10 + (p[i] - '0');
    }
    return v;
}

/// Howard Hinnant's "civil_from_days" algorithm for date->days conversion.
inline constexpr int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) noexcept {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

} // namespace detail

HelixClient::HelixClient(boost::asio::any_io_executor exec,
                         boost::asio::ssl::context&   ssl_ctx,
                         std::string_view             client_id,
                         std::string_view             client_secret) noexcept
  : executor_(exec)
  , http_(exec, ssl_ctx)
  , client_id_(client_id)
  , client_secret_(client_secret)
  , helix_expiry_(std::chrono::steady_clock::now())
{}

boost::asio::awaitable<void> HelixClient::ensureToken() noexcept {
    {
        // Fast-path: shared lock & valid token
        std::shared_lock lock(token_mutex_);
        if (!helix_token_.empty() &&
            std::chrono::steady_clock::now() < helix_expiry_) {
            co_return;
        }
    }

    // Build & send token request
    auto body = detail::makeTokenRequestBody(client_id_, client_secret_);
    static const http_client::HttpHeader hdr{
        "Content-Type", "application/x-www-form-urlencoded"
    };
    http_client::HttpHeaders headers{&hdr, 1};

    http_client::Result res;
    try {
        res = co_await http_.post(
            "id.twitch.tv", "443", "/oauth2/token", body, headers
        );
    } catch (...) {
        std::unique_lock lock(token_mutex_);
        helix_token_.clear();
        co_return;
    }

    if (!res) {
        std::unique_lock lock(token_mutex_);
        helix_token_.clear();
        co_return;
    }

    // Extract token + expiry
    auto jv = std::move(*res);
    try {
        auto token_str = jv["access_token"].get<std::string>();
        int  expires   = static_cast<int>(jv["expires_in"].get<double>());

        std::unique_lock lock(token_mutex_);
        helix_token_  = std::move(token_str);
        helix_expiry_ = std::chrono::steady_clock::now()
                      + std::chrono::seconds{expires};
    }
    catch (...) {
        std::unique_lock lock(token_mutex_);
        helix_token_.clear();
    }
    co_return;
}

boost::asio::awaitable<std::optional<StreamStartResult>>
HelixClient::getStreamStart(std::string_view channel) noexcept {
    co_await ensureToken();

    // Copy token under shared lock
    std::string token_copy;
    {
        std::shared_lock lock(token_mutex_);
        if (helix_token_.empty()) {
            co_return std::nullopt;
        }
        token_copy = helix_token_;
    }

    // Build request target
    static constexpr std::string_view prefix{"/helix/streams?user_login="};
    std::string target;
    target.reserve(prefix.size() + channel.size());
    target.append(prefix).append(channel);

    // Prepare headers
    const std::string auth = "Bearer " + token_copy;
    std::array<http_client::HttpHeader,2> hdrs{{
        {"Client-ID",     client_id_},
        {"Authorization", auth}
    }};
    http_client::HttpHeaders headers{hdrs.data(), hdrs.size()};

    // Send GET
    http_client::Result res;
    try {
        res = co_await http_.get(
            "api.twitch.tv", "443", target, headers
        );
    } catch (...) {
        co_return std::nullopt;
    }

    if (!res) {
        co_return std::nullopt;
    }

    // Parse JSON
    auto jv = std::move(*res);
    try {
        auto& data = jv["data"].get<decltype(jv)::array_t>();
        if (data.empty()) {
            co_return std::nullopt;  // channel offline
        }

        auto& first = data.front();
        auto  ts    = first["started_at"].get<std::string>();
        auto  ms    = parseIso8601Ms(ts);
        if (!ms) {
            co_return std::nullopt;
        }
        co_return StreamStartResult{true, *ms};
    }
    catch (...) {
        co_return std::nullopt;
    }
}

std::optional<std::chrono::milliseconds>
HelixClient::parseIso8601Ms(std::string_view ts) noexcept {
    // Expect exactly "YYYY-MM-DDThh:mm:ssZ"
    if (ts.size() != 20 ||
        ts[4] != '-' || ts[7] != '-' ||
        ts[10] != 'T' || ts[13] != ':' ||
        ts[16] != ':' || ts[19] != 'Z')
    {
        return std::nullopt;
    }

    int       y   = detail::parseDigits(ts.data() + 0, 4);
    unsigned  mo  = detail::parseDigits(ts.data() + 5, 2);
    unsigned  da  = detail::parseDigits(ts.data() + 8, 2);
    unsigned  h   = detail::parseDigits(ts.data() + 11, 2);
    unsigned  m   = detail::parseDigits(ts.data() + 14, 2);
    unsigned  s   = detail::parseDigits(ts.data() + 17, 2);

    int64_t days = detail::daysFromCivil(y, mo, da);
    int64_t secs = days * 86400LL + h * 3600 + m * 60 + s;
    return std::chrono::milliseconds{secs * 1000LL};
}

} // namespace twitch_bot
