#pragma once

#include "http_client.hpp"

#include <chrono>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>

namespace twitch_bot {

/// JSON value returned by Helix client.
using Json = glz::json_t;

/// Represents the `/helix/streams` response (first-element) when live.
struct StreamStartResult {
    bool                      is_live;    ///< True if the channel is live.
    std::chrono::milliseconds started_at; ///< Time (ms since UNIX epoch) when stream went live.
};

/// Minimal Helix OAuth + Streams client.
/// Thread-safe for concurrent calls; reuses pooled HTTP/TLS connections.
class HelixClient {
public:
    /**
     * @brief Construct a HelixClient.
     * @param executor      Asio executor for all async operations.
     * @param ssl_ctx       SSL context for HTTPS handshakes.
     * @param client_id     Twitch application client ID.
     * @param client_secret Twitch application client secret.
     */
    HelixClient(boost::asio::any_io_executor executor,
                boost::asio::ssl::context&   ssl_ctx,
                std::string_view             client_id,
                std::string_view             client_secret) noexcept;

    HelixClient(const HelixClient&)            = delete;
    HelixClient& operator=(const HelixClient&) = delete;
    ~HelixClient()                             = default;

    /**
     * @brief Ensure a valid OAuth token is cached (refresh if missing/expired).
     * @note Thread-safe. On any failure, the cached token is cleared.
     */
    boost::asio::awaitable<void> ensureToken() noexcept;

    /**
     * @brief Query the Helix Streams API for a channel's start time.
     * @param channel Name of the Twitch channel to query.
     * @return std::optional<StreamStartResult>
     *         - nullopt if offline or on any error.
     */
    boost::asio::awaitable<std::optional<StreamStartResult>>
    getStreamStart(std::string_view channel) noexcept;

private:
    /**
     * @brief Parse a fixed-format ISO-8601 UTC timestamp into milliseconds since epoch.
     * @param ts  Must be exactly "YYYY-MM-DDThh:mm:ssZ".
     * @return milliseconds since UNIX epoch, or nullopt on malformed input.
     */
    static std::optional<std::chrono::milliseconds>
    parseIso8601Ms(std::string_view ts) noexcept;

    boost::asio::any_io_executor           executor_;
    http_client::Client                    http_;          ///< Reusable HTTP+TLS client.
    const std::string                      client_id_;
    const std::string                      client_secret_;

    mutable std::shared_mutex              token_mutex_;   ///< Guards token / expiry.
    std::string                            helix_token_;   ///< OAuth bearer token.
    std::chrono::steady_clock::time_point  helix_expiry_;  ///< When token expires.
};

} // namespace twitch_bot
