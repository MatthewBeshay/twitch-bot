// C++ Standard Library
#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>

// Boost.Asio
#include <boost/asio/dispatch.hpp>
#include <boost/asio/use_awaitable.hpp>

// Glaze
#include <glaze/json.hpp>

// GSL
#include <gsl/gsl>

// Project
#include <tb/net/http_client.hpp>
#include <tb/twitch/helix_client.hpp>

namespace twitch_bot {

using json = glz::json_t;

namespace { // internal helpers and endpoints

    inline int parse_digits(const char* p, int n) noexcept
    {
        int v = 0;
        for (int i = 0; i < n; ++i)
            v = v * 10 + (p[i] - '0');
        return v;
    }

    constexpr int64_t days_from_civil(int64_t y, unsigned m, unsigned d) noexcept
    {
        y -= (m <= 2);
        const int64_t era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = static_cast<unsigned>(y - era * 400);
        const unsigned doy = (153 * (m + ((m > 2) ? -3 : 9)) + 2) / 5 + d - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + static_cast<int64_t>(doe) - 719468;
    }

    // Parse "YYYY-MM-DDTHH:MM:SSZ" into UNIX epoch milliseconds.
    inline std::optional<std::chrono::milliseconds> parse_iso8601_ms(std::string_view ts) noexcept
    {
        const char* p = ts.data();
        if (ts.size() != 20 || p[4] != '-' || p[7] != '-' || p[10] != 'T' || p[13] != ':'
            || p[16] != ':' || p[19] != 'Z') {
            return std::nullopt;
        }
        const int y = parse_digits(p, 4);
        const unsigned mo = static_cast<unsigned>(parse_digits(p + 5, 2));
        const unsigned d = static_cast<unsigned>(parse_digits(p + 8, 2));
        const unsigned hh = static_cast<unsigned>(parse_digits(p + 11, 2));
        const unsigned mm = static_cast<unsigned>(parse_digits(p + 14, 2));
        const unsigned ss = static_cast<unsigned>(parse_digits(p + 17, 2));

        const int64_t days = days_from_civil(y, mo, d);
        const int64_t secs = days * 86400 + static_cast<int64_t>(hh) * 3600
            + static_cast<int64_t>(mm) * 60 + static_cast<int64_t>(ss);

        return std::chrono::milliseconds(secs * 1000);
    }

    struct EndPoint {
        std::string_view host, port, target;
    };

    constexpr EndPoint oauth_validate{"id.twitch.tv", "443", "/oauth2/validate"};
    constexpr EndPoint access_token{"id.twitch.tv", "443", "/oauth2/token"};
    constexpr EndPoint helix_streams{"api.twitch.tv", "443", "/helix/streams?user_login="};

    // Percent-encode for application/x-www-form-urlencoded (no '+' for spaces).
    std::string form_urlencode(std::string_view s)
    {
        static constexpr char hex[] = "0123456789ABCDEF";
        std::string out;
        out.reserve(s.size() * 3);
        for (unsigned char c : s) {
            const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
            if (unreserved)
                out.push_back(static_cast<char>(c));
            else {
                out.push_back('%');
                out.push_back(hex[(c >> 4) & 0xF]);
                out.push_back(hex[c & 0xF]);
            }
        }
        return out;
    }

} // namespace

HelixClient::HelixClient(boost::asio::any_io_executor executor,
                         boost::asio::ssl::context& ssl_ctx,
                         std::string_view client_id,
                         std::string_view client_secret,
                         std::string_view refresh_token)
    : strand_{executor}
    , token_expiry_{std::chrono::steady_clock::now()}
    , executor_{executor}
    , http_client_{std::make_unique<http_client::client>(executor, ssl_ctx)}
    , client_id_{client_id}
    , client_secret_{client_secret}
    , refresh_token_value_(refresh_token)
{
}

HelixClient::~HelixClient() = default;

auto HelixClient::ensure_valid_token() -> boost::asio::awaitable<void>
{
    co_await boost::asio::dispatch(strand_, boost::asio::use_awaitable);

    const bool looks_fresh = !token_.empty() && std::chrono::steady_clock::now() < token_expiry_;
    if (looks_fresh && co_await validate_token()) {
        co_return;
    }

    if (!refresh_token_value_.empty()) {
        co_await refresh_token(); // user token only
    } else {
        token_.clear();
    }
}

auto HelixClient::validate_token() -> boost::asio::awaitable<bool>
{
    co_await boost::asio::dispatch(strand_, boost::asio::use_awaitable);
    if (token_.empty())
        co_return false;

    const std::string auth = "Bearer " + token_;
    std::array<http_client::http_header, 1> hdrs{{{"Authorization", auth}}};
    http_client::http_headers headers{hdrs.data(), static_cast<std::size_t>(hdrs.size())};

    try {
        auto res = co_await http_client_->get(
            oauth_validate.host, oauth_validate.port, oauth_validate.target, headers);

        if (!res)
            co_return false;

        const auto& j = res.value();
        // Update expiry if provided by validate endpoint
        const int expires_in_s = j["expires_in"].as<int>();
        token_expiry_ = std::chrono::steady_clock::now() + std::chrono::seconds{expires_in_s};
        co_return true;
    } catch (...) {
        co_return false;
    }
}

auto HelixClient::refresh_token() -> boost::asio::awaitable<void>
{
    co_return co_await fetch_token(build_refresh_token_request_body());
}

auto HelixClient::build_refresh_token_request_body() const -> std::string
{
    const std::string cid = form_urlencode(client_id_);
    const std::string csec = form_urlencode(client_secret_);
    const std::string rtok = form_urlencode(refresh_token_value_);
    return "client_id=" + cid + "&client_secret=" + csec
        + "&grant_type=refresh_token&refresh_token=" + rtok;
}

auto HelixClient::fetch_token(std::string body) -> boost::asio::awaitable<void>
{
    co_await boost::asio::dispatch(strand_, boost::asio::use_awaitable);

    std::array<http_client::http_header, 1> hdrs{
        {{"Content-Type", "application/x-www-form-urlencoded"}}};
    http_client::http_headers headers{hdrs.data(), static_cast<std::size_t>(hdrs.size())};

    try {
        auto res = co_await http_client_->post(
            access_token.host, access_token.port, access_token.target, body, headers);

        if (!res) {
            token_.clear();
            co_return;
        }

        const auto& j = res.value();
        token_ = j["access_token"].get<std::string>();

        const int expires = j["expires_in"].as<int>();
        token_expiry_ = std::chrono::steady_clock::now() + std::chrono::seconds{expires};

        if (persist_access_token_) {
            try {
                persist_access_token_(token_);
            } catch (...) { /* ignore */
            }
        }
    } catch (...) {
        token_.clear();
    }
}

auto HelixClient::get_stream_status(std::string_view channel_id)
    -> boost::asio::awaitable<std::optional<StreamStatus>>
{
    Expects(!channel_id.empty());

    co_await boost::asio::dispatch(strand_, boost::asio::use_awaitable);
    co_await ensure_valid_token();

    if (token_.empty())
        co_return std::nullopt;

    auto do_request = [&]() -> boost::asio::awaitable<std::optional<StreamStatus>> {
        std::string path;
        path.reserve(helix_streams.target.size() + channel_id.size());
        path = helix_streams.target;
        path += channel_id;

        const std::string auth = "Bearer " + token_;
        std::array<http_client::http_header, 2> hdrs{
            {{"Client-ID", client_id_}, {"Authorization", auth}}};
        http_client::http_headers headers{hdrs.data(), static_cast<std::size_t>(hdrs.size())};

        auto res
            = co_await http_client_->get(helix_streams.host, helix_streams.port, path, headers);

        if (!res)
            co_return std::nullopt;

        const auto& j = res.value();

        if (!j["data"].holds<json::array_t>())
            co_return std::nullopt;
        auto data = j["data"].get<json::array_t>();
        if (data.empty())
            co_return std::nullopt;

        auto started = data.front()["started_at"].get<std::string>();
        if (auto ms = parse_iso8601_ms(started)) {
            co_return StreamStatus{true, *ms};
        }
        co_return std::nullopt;
    };

    std::optional<StreamStatus> status;
    std::string error_msg;
    try {
        status = co_await do_request();
    } catch (std::runtime_error& e) {
        error_msg = e.what();
    } catch (...) {
        co_return std::nullopt;
    }

    if (status)
        co_return status;

    // Retry once on auth failure: clear token and refresh.
    if (!error_msg.empty() && error_msg.find("401") != std::string::npos) {
        token_.clear();
        co_await ensure_valid_token();
        try {
            status = co_await do_request();
        } catch (...) {
            status = std::nullopt;
        }
    }

    co_return status;
}

} // namespace twitch_bot
