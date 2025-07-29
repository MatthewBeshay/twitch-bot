// C++ Standard Library
#include <array>
#include <chrono>
#include <string>
#include <string_view>

// 3rd-party
#include <boost/asio/dispatch.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <gsl/gsl>

// Project
#include "helix_client.hpp"

namespace twitch_bot {

namespace { // internal helpers and endpoints

    static TB_FORCE_INLINE int parse_digits(const char* p, int n) noexcept
    {
        int v = 0;
        for (int i = 0; i < n; ++i) {
            v = v * 10 + (p[i] - '0');
        }
        return v;
    }

    static constexpr int64_t days_from_civil(int64_t y, unsigned m, unsigned d) noexcept
    {
        y -= (m <= 2);
        int64_t era = (y >= 0 ? y : y - 399) / 400;
        unsigned yoe = static_cast<unsigned>(y - era * 400);
        unsigned doy = (153 * (m + ((m > 2) ? -3 : 9)) + 2) / 5 + d - 1;
        unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + static_cast<int64_t>(doe) - 719468;
    }

    static TB_FORCE_INLINE std::optional<std::chrono::milliseconds>
    parse_iso8601_ms(std::string_view ts) noexcept
    {
        const char* p = ts.data();
        if (ts.size() != 20 || p[4] != '-' || p[7] != '-' || p[10] != 'T' || p[13] != ':'
            || p[16] != ':' || p[19] != 'Z') {
            return std::nullopt;
        }

        int y = parse_digits(p, 4);
        unsigned mo = parse_digits(p + 5, 2);
        unsigned d = parse_digits(p + 8, 2);
        unsigned hh = parse_digits(p + 11, 2);
        unsigned mm = parse_digits(p + 14, 2);
        unsigned ss = parse_digits(p + 17, 2);

        int64_t days = days_from_civil(y, mo, d);
        int64_t secs = days * 86400 + static_cast<int64_t>(hh) * 3600
            + static_cast<int64_t>(mm) * 60 + static_cast<int64_t>(ss);

        return std::chrono::milliseconds(secs * 1000);
    }

    struct EndPoint {
        std::string_view host;
        std::string_view port;
        std::string_view target;
    };

    constexpr EndPoint oauth_validate{"id.twitch.tv", "443", "/oauth2/validate"};
    constexpr EndPoint oauth_token{"id.twitch.tv", "443", "/oauth2/token"};
    constexpr EndPoint helix_streams{"api.twitch.tv", "443", "/helix/streams?user_login="};

} // namespace

HelixClient::HelixClient(boost::asio::any_io_executor executor,
                         boost::asio::ssl::context& ssl_ctx,
                         std::string_view client_id,
                         std::string_view client_secret,
                         std::string_view refresh_token) noexcept
    : strand_{executor}
    , token_expiry_{std::chrono::steady_clock::now()}
    , executor_{executor}
    , http_client_{executor, ssl_ctx}
    , client_id_{client_id}
    , client_secret_{client_secret}
    , refresh_token_(refresh_token)
{
}

auto HelixClient::ensure_valid_token() noexcept -> boost::asio::awaitable<void>
{
    co_await boost::asio::dispatch(strand_, boost::asio::use_awaitable);

    bool is_fresh = !token_.empty() && std::chrono::steady_clock::now() < token_expiry_;

    if (is_fresh && co_await validate_token()) {
        co_return;
    }

    if (!refresh_token_.empty()) {
        co_await refresh_token();
    } else {
        co_await request_new_token();
    }
}

auto HelixClient::validate_token() noexcept -> boost::asio::awaitable<bool>
{
    co_await boost::asio::dispatch(strand_, boost::asio::use_awaitable);

    if (token_.empty()) {
        co_return false;
    }

    std::string auth = "Bearer " + token_;
    std::array<http_client::http_header, 1> hdrs{{{"Authorization", auth}}};
    http_client::http_headers headers{hdrs.data(), int(hdrs.size())};

    try {
        auto result = co_await http_client_.get(
            oauth_validate.host, oauth_validate.port, oauth_validate.target, headers);

        if (!result) {
            co_return false;
        }

        int expires_in_s = (*result)["expires_in"].as<int>();
        token_expiry_ = std::chrono::steady_clock::now() + std::chrono::seconds{expires_in_s};

        co_return true;
    } catch (...) {
        co_return false;
    }
}

auto HelixClient::request_new_token() noexcept -> boost::asio::awaitable<void>
{
    co_return co_await fetch_token(build_client_credentials_request_body());
}

auto HelixClient::refresh_token() noexcept -> boost::asio::awaitable<void>
{
    co_return co_await fetch_token(build_refresh_token_request_body());
}

auto HelixClient::build_client_credentials_request_body() const noexcept -> std::string
{
    return "client_id=" + client_id_ + "&client_secret=" + client_secret_
        + "&grant_type=client_credentials";
}

auto HelixClient::build_refresh_token_request_body() const noexcept -> std::string
{
    return "client_id=" + client_id_ + "&client_secret=" + client_secret_
        + "&grant_type=refresh_token"
          "&refresh_token="
        + refresh_token_;
}

auto HelixClient::fetch_token(std::string body) noexcept -> boost::asio::awaitable<void>
{
    co_await boost::asio::dispatch(strand_, boost::asio::use_awaitable);

    std::array<http_client::http_header, 1> hdrs{
        {{"Content-Type", "application/x-www-form-urlencoded"}}};
    http_client::http_headers headers{hdrs.data(), int(hdrs.size())};

    try {
        auto result = co_await http_client_.post(
            oauth_token.host, oauth_token.port, oauth_token.target, body, headers);
        if (!result) {
            token_.clear();
            co_return;
        }

        token_ = (*result)["access_token"].get<std::string>();
        if ((*result)["refresh_token"].holds<std::string>()) {
            refresh_token_ = (*result)["refresh_token"].get<std::string>();
        }

        int expires = (*result)["expires_in"].as<int>();
        token_expiry_ = std::chrono::steady_clock::now() + std::chrono::seconds{expires};
    } catch (...) {
        token_.clear();
    }
}

auto HelixClient::get_stream_status(std::string_view channel_id) noexcept
    -> boost::asio::awaitable<std::optional<StreamStatus>>
{
    Expects(!channel_id.empty());

    co_await boost::asio::dispatch(strand_, boost::asio::use_awaitable);
    co_await ensure_valid_token();

    if (token_.empty()) {
        co_return std::nullopt;
    }

    auto do_request = [&]() -> boost::asio::awaitable<std::optional<StreamStatus>> {
        std::string path;
        path.reserve(helix_streams.target.size() + channel_id.size());
        path = helix_streams.target;
        path += channel_id;

        std::string auth = "Bearer " + token_;
        std::array<http_client::http_header, 2> hdrs{
            {{"Client-ID", client_id_}, {"Authorization", auth}}};
        http_client::http_headers headers{hdrs.data(), int(hdrs.size())};

        auto result
            = co_await http_client_.get(helix_streams.host, helix_streams.port, path, headers);
        if (!result) {
            co_return std::nullopt;
        }

        auto& j = *result;
        if (!j["data"].holds<json::array_t>()) {
            co_return std::nullopt;
        }
        auto data = j["data"].get<json::array_t>();
        if (data.empty()) {
            co_return std::nullopt;
        }

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

    if (status) {
        co_return status;
    }

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
