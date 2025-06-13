#include "helix_client.hpp"
#include "utils/attributes.hpp"

#include <array>
#include <system_error>

#include <boost/asio/use_awaitable.hpp>
#include <boost/beast/version.hpp>
#include <openssl/ssl.h>

namespace twitch_bot {
namespace detail {

/// Build body for token request.
inline std::string
make_token_body(std::string_view client_id,
                std::string_view client_secret) noexcept
{
    std::string body;
    body.reserve(64);
    body   = "client_id=";
    body  += client_id;
    body  += "&client_secret=";
    body  += client_secret;
    body  += "&grant_type=client_credentials";
    return body;
}

/// Parse `n` decimal digits from `p`.
inline int
parse_digits(const char* TB_RESTRICT p, int n) noexcept
{
    int value = 0;
    for (int i = 0; i < n; ++i)
    {
        value = value * 10 + (p[i] - '0');
    }
    return value;
}

/// Days since civil epoch (1970-01-01).
inline constexpr int64_t
days_from_civil(int64_t y,
                unsigned m,
                unsigned d) noexcept
{
    y -= m <= 2;
    const int64_t era  = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5
                         + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

}   // namespace detail

HelixClient::HelixClient(boost::asio::any_io_executor executor,
                           boost::asio::ssl::context&   ssl_ctx,
                           std::string_view             client_id,
                           std::string_view             client_secret) noexcept
    : executor_(executor)
    , http_client_(executor_, ssl_ctx)
    , client_id_(client_id)
    , client_secret_(client_secret)
    , token_expiry_(std::chrono::steady_clock::now())
{
    token_body_   = detail::make_token_body(client_id_, client_secret_);
    target_path_.reserve(64);
    auth_header_.reserve(64);
}

boost::asio::awaitable<void>
HelixClient::ensure_token() noexcept
{
    {
        std::shared_lock lock{token_mutex_};
        if (!token_.empty() &&
            std::chrono::steady_clock::now() < token_expiry_)
        {
            co_return;
        }
    }

    static constexpr std::string_view k_token_host   = "id.twitch.tv";
    static constexpr std::string_view k_token_port   = "443";
    static constexpr std::string_view k_token_target = "/oauth2/token";

    static const http_client::http_header header{
        "Content-Type", "application/x-www-form-urlencoded"
    };
    http_client::http_headers headers{&header, 1};

    http_client::result result;
    try
    {
        result = co_await http_client_.post(
            k_token_host,
            k_token_port,
            k_token_target,
            token_body_,
            headers
        );
    }
    catch (...)
    {
        std::unique_lock lock{token_mutex_};
        token_.clear();
        co_return;
    }

    if (!result)
    {
        std::unique_lock lock{token_mutex_};
        token_.clear();
        co_return;
    }

    auto json_val = std::move(*result);
    try
    {
        auto access_token = json_val["access_token"].get<std::string>();
        int  expires_in   = detail::parse_digits(
            json_val["expires_in"].get<std::string>().data(), 10
        );

        std::unique_lock lock{token_mutex_};
        token_        = std::move(access_token);
        token_expiry_ = std::chrono::steady_clock::now()
                        + std::chrono::seconds{expires_in};
    }
    catch (...)
    {
        std::unique_lock lock{token_mutex_};
        token_.clear();
    }

    co_return;
}

boost::asio::awaitable<std::optional<stream_start_result>>
HelixClient::get_stream_start(std::string_view channel_id) noexcept
{
    co_await ensure_token();

    {
        std::shared_lock lock{token_mutex_};
        if (token_.empty())
        {
            co_return std::nullopt;
        }
        auth_header_.assign("Bearer ");
        auth_header_.append(token_);
    }

    static constexpr std::string_view k_helix_host   = "api.twitch.tv";
    static constexpr std::string_view k_helix_port   = "443";
    static constexpr std::string_view k_helix_prefix = "/helix/streams?user_login=";

    target_path_.clear();
    target_path_ += k_helix_prefix;
    target_path_ += channel_id;

    std::array<http_client::http_header, 2> hdrs{{
        {"Client-ID",     client_id_},
        {"Authorization", auth_header_}
    }};
    http_client::http_headers headers{hdrs.data(), hdrs.size()};

    http_client::result result;
    try
    {
        result = co_await http_client_.get(
            k_helix_host,
            k_helix_port,
            target_path_,
            headers
        );
    }
    catch (...)
    {
        co_return std::nullopt;
    }

    if (!result)
    {
        co_return std::nullopt;
    }

    auto json_val = std::move(*result);
    try
    {
        auto& data_array = json_val["data"].get<decltype(json_val)::array_t>();
        if (data_array.empty())
        {
            co_return std::nullopt;
        }

        auto& first = data_array.front();
        auto  ts    = first["started_at"].get<std::string>();
        auto  ms    = parse_iso8601_ms(ts);
        if (!ms)
        {
            co_return std::nullopt;
        }

        co_return stream_start_result{true, *ms};
    }
    catch (...)
    {
        co_return std::nullopt;
    }
}

std::optional<std::chrono::milliseconds>
HelixClient::parse_iso8601_ms(std::string_view timestamp) noexcept
{
    // Expected format: "YYYY-MM-DDThh:mm:ssZ"
    if (timestamp.size() != 20  ||
        timestamp[4]   != '-' ||
        timestamp[7]   != '-' ||
        timestamp[10]  != 'T' ||
        timestamp[13]  != ':' ||
        timestamp[16]  != ':' ||
        timestamp[19]  != 'Z')
    {
        return std::nullopt;
    }

    int       year   = detail::parse_digits(timestamp.data() + 0, 4);
    unsigned  month  = detail::parse_digits(timestamp.data() + 5, 2);
    unsigned  day    = detail::parse_digits(timestamp.data() + 8, 2);
    unsigned  hour   = detail::parse_digits(timestamp.data() + 11, 2);
    unsigned  minute = detail::parse_digits(timestamp.data() + 14, 2);
    unsigned  second = detail::parse_digits(timestamp.data() + 17, 2);

    int64_t days = detail::days_from_civil(year, month, day);
    int64_t secs = days * 86400LL +
                   hour * 3600     +
                   minute * 60     +
                   second;
    return std::chrono::milliseconds{secs * 1000LL};
}

}   // namespace twitch_bot
