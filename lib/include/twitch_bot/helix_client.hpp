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

/// JSON from Helix API.
using json = glz::json_t;

/// Data returned when channel goes live.
struct stream_start_result {
    bool is_live{false}; ///< true if channel is live
    std::chrono::milliseconds start_time{0}; ///< ms since UNIX epoch when live
};

/// OAuth2 + streams client for Twitch Helix.
/// Thread-safe; reuses pooled HTTP/TLS connections.
class HelixClient
{
public:
    /// Construct client.
    HelixClient(boost::asio::any_io_executor executor,
                boost::asio::ssl::context &ssl_ctx,
                std::string_view client_id,
                std::string_view client_secret) noexcept;

    // No copies, but allow moves
    HelixClient(const HelixClient &) = delete;
    HelixClient &operator=(const HelixClient &) = delete;

    HelixClient(HelixClient &&) noexcept = default;
    HelixClient &operator=(HelixClient &&) noexcept = default;

    ~HelixClient() = default;

    /// Ensure valid OAuth2 token.
    auto ensure_token() noexcept -> boost::asio::awaitable<void>;

    /// Get stream start for given channel.
    auto get_stream_start(std::string_view channel_id) noexcept
        -> boost::asio::awaitable<std::optional<stream_start_result>>;

private:
    /// Parse ISO-8601 "YYYY-MM-DDThh:mm:ssZ" to ms since epoch.
    TB_FORCE_INLINE
    static auto parse_iso8601_ms(std::string_view timestamp) noexcept
        -> std::optional<std::chrono::milliseconds>;

    // Prebuilt buffers
    std::string token_body_;
    std::string target_path_;
    std::string auth_header_;

    // Asio executor and HTTP client
    boost::asio::any_io_executor executor_;
    http_client::client http_client_;

    // Application credentials
    const std::string client_id_;
    const std::string client_secret_;

    mutable std::shared_mutex token_mutex_;
    std::string token_;
    std::chrono::steady_clock::time_point token_expiry_;
};

} // namespace twitch_bot
