#include "helix_client.hpp"
#include "http_client.hpp"

#include <iomanip>
#include <sstream>

#include <boost/json.hpp>
#include <boost/asio/this_coro.hpp>

namespace twitch_bot {

HelixClient::HelixClient(asio::io_context&   ioc,
                         asio::ssl::context& ssl_ctx,
                         std::string_view    client_id,
                         std::string_view    client_secret)
  : ioc_(ioc)
  , ssl_ctx_(ssl_ctx)
  , client_id_(client_id)
  , client_secret_(client_secret)
  , helix_token_()
  , helix_expiry_(std::chrono::steady_clock::now())
{}

asio::awaitable<void> HelixClient::ensureToken()
{
    using namespace std::chrono;
    auto now = steady_clock::now();
    // If we already have a non-empty token that hasn't expired, return immediately.
    if (!helix_token_.empty() && now < helix_expiry_) {
        co_return;
    }

    // Build POST body: client_id=...&client_secret=...&grant_type=client_credentials
    std::string body = "client_id=" + client_id_ +
                       "&client_secret=" + client_secret_ +
                       "&grant_type=client_credentials";

    json::value jv;
    try {
        jv = co_await http_client::post(
            "id.twitch.tv", "443", "/oauth2/token",
            body,
            {{"Content-Type", "application/x-www-form-urlencoded"}},
            ioc_, ssl_ctx_
        );
    }
    catch (const boost::system::system_error&) {
        // Network or TLS error: clear token and return
        helix_token_.clear();
        co_return;
    }
    catch (const std::runtime_error&) {
        // Non-200 status or JSON parse error: clear token and return
        helix_token_.clear();
        co_return;
    }

    // Parse the JSON response: { "access_token": "...", "expires_in": 3600, ... }
    try {
        auto& obj = jv.as_object();
        helix_token_ = std::string(obj.at("access_token").as_string());
        int expires_in = obj.at("expires_in").to_number<int>();
        helix_expiry_ = now + seconds(expires_in);
    }
    catch (const std::exception&) {
        // Missing fields or wrong type: clear token
        helix_token_.clear();
    }

    co_return;
}

asio::awaitable<std::optional<StreamStartResult>> HelixClient::getStreamStart(
    std::string_view channel)
{
    // Ensure we have a valid OAuth token
    co_await ensureToken();
    if (helix_token_.empty()) {
        co_return std::nullopt;
    }

    // Build GET target: "/helix/streams?user_login=<channel>"
    std::string target = "/helix/streams?user_login=" + std::string(channel);

    json::value jv;
    try {
        jv = co_await http_client::get(
            "api.twitch.tv", "443", target,
            {
                {"Client-ID", client_id_},
                {"Authorization", "Bearer " + helix_token_}
            },
            ioc_, ssl_ctx_
        );
    }
    catch (const boost::system::system_error&) {
        // Network/TLS error -> treat as offline
        co_return std::nullopt;
    }
    catch (const std::runtime_error&) {
        // Non-200 status or JSON parse error -> treat as offline
        co_return std::nullopt;
    }

    // Inspect "data" array in the JSON
    try {
        auto& rootObj = jv.as_object();
        auto& dataArr = rootObj.at("data");
        if (dataArr.kind() != json::kind::array || dataArr.as_array().empty()) {
            co_return std::nullopt; // channel is offline
        }

        // Take the first element and parse "started_at" (ISO 8601, UTC)
        auto& firstObj = dataArr.as_array()[0].as_object();
        std::string started_at = std::string(firstObj.at("started_at").as_string());

        // Parse "YYYY-MM-DDTHH:MM:SSZ" into std::tm (UTC)
        std::tm tm{};
        std::istringstream ss(started_at);
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        if (ss.fail()) {
            co_return std::nullopt;
        }

        // Convert std::tm (UTC) to time_t epoch seconds
        time_t epoch_secs;
    #if defined(_WIN32) || defined(_WIN64)
        epoch_secs = _mkgmtime(&tm);
    #else
        epoch_secs = timegm(&tm);
    #endif

        auto started_ms = std::chrono::milliseconds{static_cast<long long>(epoch_secs) * 1000LL};
        co_return StreamStartResult{ true, started_ms };
    }
    catch (const std::exception&) {
        co_return std::nullopt;
    }
}

} // namespace twitch_bot
