// helix_client.cpp
#include "helix_client.hpp"

#include <array>
#include <chrono>
#include <string>
#include <string_view>

namespace twitch_bot {

using namespace std::chrono;
static constexpr std::string_view grant_sv{"&grant_type=client_credentials"};

/// Build form body in one allocation.
static std::string make_token_body(std::string_view cid,
                                   std::string_view secret) noexcept
{
    // "client_id="<cid>"&client_secret="<secret><grant>"
    std::string s;
    s.reserve(10 + cid.size()
              + 15 + secret.size()
              + grant_sv.size());
    s.append("client_id=").append(cid)
     .append("&client_secret=").append(secret)
     .append(grant_sv);
    return s;
}

/// Helper: parse exactly n decimal digits at p.
static int parse_digits(const char* p, int n) noexcept {
    int v = 0;
    for (int i = 0; i < n; ++i) {
        v = v * 10 + (p[i] - '0');
    }
    return v;
}

HelixClient::HelixClient(boost::asio::any_io_executor exec,
                         boost::asio::ssl::context& ssl_ctx,
                         std::string_view client_id,
                         std::string_view client_secret)
  : executor_(exec)
  , http_(exec, ssl_ctx)
  , client_id_(client_id)
  , client_secret_(client_secret)
  , helix_expiry_(steady_clock::now())
{}

boost::asio::awaitable<void> HelixClient::ensureToken() {
    // Quick check under shared lock
    {
        std::shared_lock lock{token_mutex_};
        if (!helix_token_.empty() &&
            steady_clock::now() < helix_expiry_)
        {
            co_return;
        }
    }

    // Build and POST the token request
    auto body = make_token_body(client_id_, client_secret_);
    static http_client::Header hdr{"Content-Type",
                                   "application/x-www-form-urlencoded"};
    http_client::Headers headers{&hdr, 1};

    json jv;
    try {
        jv = co_await http_.post(
            "id.twitch.tv", "443", "/oauth2/token",
            body, headers);
    }
    catch (...) {
        // On error, clear token under exclusive lock
        std::unique_lock lock{token_mutex_};
        helix_token_.clear();
        co_return;
    }

    // Extract and stash the new token + expiry
    try {
        auto& obj    = jv.as_object();
        auto token_sv= obj.at("access_token").as_string();
        auto expires = obj.at("expires_in").to_number<int>();
        auto new_exp  = steady_clock::now() + seconds(expires);

        std::unique_lock lock{token_mutex_};
        helix_token_.assign(token_sv.data(), token_sv.size());
        helix_expiry_ = new_exp;
    }
    catch (...) {
        std::unique_lock lock{token_mutex_};
        helix_token_.clear();
    }
    co_return;
}

std::optional<std::chrono::milliseconds>
HelixClient::parseIso8601Ms(std::string_view ts) noexcept {
    // Expect "YYYY-MM-DDTHH:MM:SSZ"
    if (ts.size() != 20 ||
        ts[4]  != '-' || ts[7]  != '-' ||
        ts[10] != 'T' || ts[13] != ':' ||
        ts[16] != ':' || ts[19] != 'Z')
    {
        return std::nullopt;
    }

    std::tm tm{};
    tm.tm_year = parse_digits(ts.data()+0, 4) - 1900;
    tm.tm_mon  = parse_digits(ts.data()+5, 2) - 1;
    tm.tm_mday= parse_digits(ts.data()+8, 2);
    tm.tm_hour= parse_digits(ts.data()+11,2);
    tm.tm_min = parse_digits(ts.data()+14,2);
    tm.tm_sec = parse_digits(ts.data()+17,2);
    tm.tm_isdst = 0;

    time_t tt;
  #if defined(_WIN32) || defined(_WIN64)
    tt = _mkgmtime(&tm);
  #else
    tt = timegm(&tm);
  #endif

    if (tt < 0) return std::nullopt;
    return std::chrono::milliseconds(static_cast<long long>(tt) * 1000LL);
}

boost::asio::awaitable<std::optional<StreamStartResult>>
HelixClient::getStreamStart(std::string_view channel) {
    co_await ensureToken();

    // Grab a copy of the token under shared lock
    std::string token_copy;
    {
        std::shared_lock lock{token_mutex_};
        if (helix_token_.empty()) {
            co_return std::nullopt;
        }
        token_copy = helix_token_;
    }

    // Build the request URI in one go
    static constexpr std::string_view prefix{
        "/helix/streams?user_login="
    };
    std::string target;
    target.reserve(prefix.size() + channel.size());
    target.append(prefix).append(channel);

    // Prepare headers
    std::string auth = "Bearer " + token_copy;
    std::array<http_client::Header,2> hdrs{{
        {"Client-ID",     client_id_},
        {"Authorization", auth}
    }};
    http_client::Headers headers{hdrs.data(), hdrs.size()};

    // Fetch and parse
    json jv;
    try {
        jv = co_await http_.get(
            "api.twitch.tv", "443", target, headers);
    }
    catch (...) {
        co_return std::nullopt;
    }

    // Examine the JSON response
    try {
        auto& root = jv.as_object();
        auto& data = root.at("data").as_array();
        if (data.empty()) {
            co_return std::nullopt;  // offline
        }
        auto& first    = data[0].as_object();
        auto ts_sv     = first.at("started_at").as_string();
        auto ms_opt    = parseIso8601Ms(ts_sv);
        if (!ms_opt) {
            co_return std::nullopt;
        }
        co_return StreamStartResult{ true, *ms_opt };
    }
    catch (...) {
        co_return std::nullopt;
    }
}

} // namespace twitch_bot
