#pragma once

// C++ Standard Library
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// 3rd-party
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ssl/context.hpp>
#include <glaze/json.hpp>

// Project
#include <tb/net/http/http_client.hpp>

namespace faceit {

/// Glaze JSON alias
using json = glz::json_t;

/// High-performance client for FACEIT’s Data (v4) and Stats (v1) APIs.
///
/// - **v4 Data API** (open.faceit.com/data/v4) requires
///   `Authorization: Bearer <API_KEY>` and `Accept: application/json`
/// - **v1 Stats API** (api.faceit.com/stats/v1) must send **no** headers
///   (to avoid intermittent HTTP 500s).
class Client
{
public:
    /// Construct with executor, SSL context, and v4 API key.
    /// @param executor  Asio executor for async operations.
    /// @param ssl_ctx   SSL context for TLS.
    /// @param apiKey    FACEIT v4 API key (must be non-empty).
    /// @throws std::invalid_argument if `apiKey` is empty.
    explicit Client(boost::asio::any_io_executor executor,
                    boost::asio::ssl::context& ssl_ctx,
                    std::string apiKey);

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;

    /// Lookup a player by nickname (v4).
    /// @see https://docs.faceit.com/docs/data-api/data/#tag/Players/operation/getPlayerFromLookup
    /// @param nickname  FACEIT display name.
    /// @param game      Game slug (default `"cs2"`).
    /// @returns Parsed JSON object of player details.
    [[nodiscard]] boost::asio::awaitable<json>
    get_player_by_nickname(std::string_view nickname, std::string_view game = "cs2");

    /// Lookup a player by FACEIT ID (v4).
    /// @see https://docs.faceit.com/docs/data-api/data/#tag/Players/operation/getPlayer
    /// @param playerId  FACEIT player UUID.
    /// @returns Parsed JSON object of player details.
    [[nodiscard]] boost::asio::awaitable<json> get_player_by_id(std::string_view playerId);

    /// Fetch per-match stats (v4).
    ///
    /// Endpoint: `/data/v4/players/{playerId}/games/{game}/stats`
    /// Query parameters:
    /// - `from` (ms since epoch) **inclusive** match finished time
    /// - `limit` (1–100)
    /// - `to` (ms since epoch) **optional** exclusive match finished time
    ///
    /// @see https://docs.faceit.com/docs/data-api/data/#tag/Players/operation/getPlayerStats
    /// @param playerId  FACEIT player UUID.
    /// @param fromTs    Lower bound timestamp (ms since epoch).
    /// @param toTs      Optional upper bound timestamp (ms since epoch).
    /// @param limit     Maximum items to return (default 100).
    /// @returns Vector of JSON objects (one per match).
    [[nodiscard]] boost::asio::awaitable<std::vector<json>>
    get_player_stats(std::string_view playerId,
                     int64_t fromTs,
                     std::optional<int64_t> toTs = std::nullopt,
                     int limit = 100);

    /// Fetch match-by-match ELO history (v1, public).
    ///
    /// Endpoint: `/stats/v1/stats/time/users/{playerId}/games/{game}`
    /// Query parameters:
    /// - `size` page size (up to 2000)
    /// - `page` zero-based index
    /// - `from` (ms since epoch) **inclusive**
    /// - `to`   (ms since epoch) **inclusive**
    ///
    /// @see https://docs.faceit.com/docs/data-api/data/#tag/Stats/operation/getUserGameStatsByTime
    /// @param playerId  FACEIT player UUID.
    /// @param size      Items per page.
    /// @param page      Zero-based page index.
    /// @param fromMs    Optional lower bound (ms since epoch).
    /// @param toMs      Optional upper bound (ms since epoch).
    /// @returns Vector of JSON history entries.
    [[nodiscard]] boost::asio::awaitable<std::vector<json>>
    get_elo_history(std::string_view playerId,
                    int size,
                    int page,
                    std::optional<int64_t> fromMs = std::nullopt,
                    std::optional<int64_t> toMs = std::nullopt);

    /// Fetch detailed match stats (v4).
    /// @see https://docs.faceit.com/docs/data-api/data/#tag/Matches/operation/getMatchStats
    /// @param matchId   FACEIT match UUID.
    /// @returns Parsed JSON object of match statistics.
    [[nodiscard]] boost::asio::awaitable<json> get_match_stats(std::string_view matchId);

    /// Fetch details about a match (v4).
    /// @see https://docs.faceit.com/docs/data-api/data/#tag/Matches/operation/getMatch
    /// @param matchId   FACEIT match UUID.
    /// @returns Parsed JSON object of match details.
    [[nodiscard]] boost::asio::awaitable<json> get_match_details(std::string_view matchId);

private:
    /// Internal: send GET to v4 (Data API) with Bearer+Accept headers.
    boost::asio::awaitable<json> send_v4_request(std::string_view host, std::string target);

    /// Internal: send GET to v1 (Stats API) with **no** headers.
    boost::asio::awaitable<json> send_v1_request(std::string_view host, std::string target);

    /// Build `"/path?key=val&…"` with RFC3986 percent-encoding.
    static std::string
    build_target(std::string_view base,
                 const std::vector<std::pair<std::string_view, std::string>>& qs);

    /// Percent-encode per RFC3986.
    static std::string url_encode(std::string_view value) noexcept;

    std::string api_key_;
    http_client::client http_;
};

} // namespace faceit
