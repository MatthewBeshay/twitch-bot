#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <chrono>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/json.hpp>

namespace twitch_bot {

namespace asio = boost::asio;
namespace json = boost::json;

// Represents the result of querying "/helix/streams?user_login=...".
// If the channel is live, 'started_at' is when it started (milliseconds since epoch).
// If not live, 'is_live' == false.
struct StreamStartResult {
    bool                          is_live;
    std::chrono::milliseconds     started_at;
};

// A minimal Helix OAuth + Streams client.
//
// Usage pattern:
//   1) co_await helixClient.ensureToken();
//   2) co_await helixClient.getStreamStart("some_channel");
//
// If the token hasn't been fetched yet or has expired, ensureToken() fetches a new one
// from id.twitch.tv/oauth2/token. Otherwise it does nothing.
//
// getStreamStart(...) returns:
//   - std::nullopt if the channel is offline or on any error.
//   - a StreamStartResult if the channel is live.
class HelixClient {
public:
    // Construct a HelixClient using the given I/O and SSL contexts.
    //
    // @param ioc           The asio::io_context to drive I/O.
    // @param ssl_ctx       The asio::ssl::context for TLS.
    // @param client_id     Twitch App client ID (Helix).
    // @param client_secret Twitch App client secret (Helix).
    HelixClient(asio::io_context&     ioc,
                asio::ssl::context&   ssl_ctx,
                std::string_view      client_id,
                std::string_view      client_secret);

    ~HelixClient() = default;

    HelixClient(const HelixClient&) = delete;
    HelixClient& operator=(const HelixClient&) = delete;

    HelixClient(HelixClient&&) noexcept = default;
    HelixClient& operator=(HelixClient&&) noexcept = default;

    // If there is no token yet, or if it has expired, obtain a new one
    // via POST /oauth2/token. Sets 'helix_token_' and 'helix_expiry_'.
    //
    // After this call, 'helix_token_' is valid until 'helix_expiry_'.
    asio::awaitable<void> ensureToken();

    // Once ensureToken() has been called successfully, do GET
    // /helix/streams?user_login=<channel>. If the "data" array is empty,
    // return std::nullopt. Otherwise parse "started_at"
    // (ISO timestamp) into a std::chrono::milliseconds since epoch.
    //
    // @param channel  The channel name (without '#') to query.
    // @return         std::nullopt if offline or on error; else StreamStartResult.
    asio::awaitable<std::optional<StreamStartResult>>
    getStreamStart(std::string_view channel);

private:
    // The I/O and SSL contexts (injected from TwitchBot).
    asio::io_context&       ioc_;
    asio::ssl::context&     ssl_ctx_;

    // Client credentials for OAuth.
    std::string             client_id_;
    std::string             client_secret_;

    // Stored OAuth token and expiry time.
    std::string             helix_token_;
    std::chrono::steady_clock::time_point helix_expiry_;
};

} // namespace twitch_bot
