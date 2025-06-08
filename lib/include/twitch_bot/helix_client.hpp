#pragma once

#include "http_client.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <chrono>
#include <shared_mutex>

namespace twitch_bot {

using json = boost::json::value;

/// Result of querying /helix/streams?user_login=...
struct StreamStartResult {
    bool                          is_live;
    std::chrono::milliseconds     started_at;
};

/// Minimal Helix OAuth + Streams client.
/// Thread-safe for concurrent calls; reuses HTTP connections.
class HelixClient {
public:
    /// @param exec          Executor for all async ops
    /// @param ssl_ctx       TLS context for HTTPS
    /// @param client_id     Helix client ID
    /// @param client_secret Helix client secret
    HelixClient(boost::asio::any_io_executor exec,
                boost::asio::ssl::context&   ssl_ctx,
                std::string_view             client_id,
                std::string_view             client_secret);

    HelixClient(const HelixClient&) = delete;
    HelixClient& operator=(const HelixClient&) = delete;
    ~HelixClient() = default;

    /// Ensure we have a valid OAuth token (refresh if missing/expired).
    /// Clears token on any failure.
    boost::asio::awaitable<void> ensureToken();

    /// Query stream start time. Returns nullopt if offline or on error.
    boost::asio::awaitable<std::optional<StreamStartResult>>
    getStreamStart(std::string_view channel);

private:
    /// Parse fixed-format ISO-8601 into ms since UNIX epoch.
    static std::optional<std::chrono::milliseconds>
    parseIso8601Ms(std::string_view ts) noexcept;

    boost::asio::any_io_executor executor_;
    http_client::Client         http_;           // pooled HTTP/TLS client
    const std::string           client_id_;
    const std::string           client_secret_;

    std::shared_mutex                               token_mutex_;
    std::string                                    helix_token_;
    std::chrono::steady_clock::time_point          helix_expiry_;
};

} // namespace twitch_bot
