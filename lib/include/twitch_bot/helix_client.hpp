#pragma once

// C++ Standard Library
#include <chrono>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>

// 3rd-party
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ssl/context.hpp>

// Project
#include "http_client.hpp"
#include "utils/attributes.hpp"

namespace twitch_bot {

/// JSON returned by the Helix API.
using json = glz::json_t;

/// Stream-live flag and UTC start time in milliseconds.
struct stream_start_result {
    bool is_live{false};
    std::chrono::milliseconds start_time{0};
};

/// Helix client with pooled HTTPS connections and automatic OAuth2 refresh.
class HelixClient
{
public:
    HelixClient(boost::asio::any_io_executor executor,
                boost::asio::ssl::context& ssl_ctx,
                std::string_view client_id,
                std::string_view client_secret) noexcept;

    HelixClient(const HelixClient&) = delete;
    HelixClient& operator=(const HelixClient&) = delete;
    HelixClient(HelixClient&&) noexcept = default;
    HelixClient& operator=(HelixClient&&) noexcept = default;
    ~HelixClient() = default;

    /// Ensure \ref token_ is valid, refreshing if expired.
    auto ensure_token() noexcept -> boost::asio::awaitable<void>;

    /// Live status and start time for \p channel_id.
    auto get_stream_start(std::string_view channel_id) noexcept
        -> boost::asio::awaitable<std::optional<stream_start_result>>;

private:
    /// Parse “YYYY-MM-DDThh:mm:ssZ” to milliseconds since epoch.
    TB_FORCE_INLINE
    static auto parse_iso8601_ms(std::string_view timestamp) noexcept
        -> std::optional<std::chrono::milliseconds>;

    // Pre-built request buffers
    std::string token_body_;
    std::string target_path_;
    std::string auth_header_;

    boost::asio::any_io_executor executor_;
    http_client::client http_client_;

    const std::string client_id_;
    const std::string client_secret_;

    mutable std::shared_mutex token_mutex_;
    std::string token_;
    std::chrono::steady_clock::time_point token_expiry_;
};

} // namespace twitch_bot
