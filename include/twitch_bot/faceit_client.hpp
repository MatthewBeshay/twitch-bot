#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/json.hpp>

namespace faceit {

/// JSON result type (Boost.JSON)
using json = boost::json::value;

/// @brief High-performance client for FACEIT v4/v1 Data APIs over HTTPS.
///        All methods are coroutine-based and return awaitable results.
/// @throws std::invalid_argument on empty API key.
class Client {
public:
    /// @param apiKey  Non-empty FACEIT API key (v4).
    explicit Client(std::string apiKey);

    /// @brief Lookup a player by nickname (v4).
    /// @param nickname  Player’s display name.
    /// @param game      Game slug (default "cs2").
    /// @returns Parsed JSON of player details.
    boost::asio::awaitable<json>
    getPlayerByNickname(std::string_view nickname,
                        std::string_view game = "cs2");

    /// @brief Lookup a player by FACEIT ID (v4).
    /// @param playerId  FACEIT player UUID.
    /// @returns Parsed JSON of player details.
    boost::asio::awaitable<json>
    getPlayerById(std::string_view playerId);

    /// @brief Fetch per-match stats for a player (v4).
    /// @param playerId  FACEIT player UUID.
    /// @param fromTs    UNIX timestamp (inclusive).
    /// @param toTs      Optional UNIX timestamp (exclusive).
    /// @param limit     Max number of entries (default 100).
    /// @returns Vector of per-match JSON stats.
    boost::asio::awaitable<std::vector<json>>
    getPlayerStats(std::string_view        playerId,
                   int64_t                fromTs,
                   std::optional<int64_t> toTs    = std::nullopt,
                   int                    limit   = 100);

    /// @brief Fetch ELO history (v1, public).
    /// @param playerId  FACEIT player UUID.
    /// @param size      Entries per page.
    /// @param page      Page index (0-based).
    /// @param fromTs    Optional UNIX timestamp (inclusive).
    /// @param toTs      Optional UNIX timestamp (exclusive).
    /// @returns Vector of JSON entries.
    boost::asio::awaitable<std::vector<json>>
    getEloHistory(std::string_view        playerId,
                  int                     size,
                  int                     page,
                  std::optional<int64_t>  fromTs = std::nullopt,
                  std::optional<int64_t>  toTs   = std::nullopt);

    /// @brief Fetch detailed match stats (v4).
    /// @param matchId   FACEIT match UUID.
    /// @returns Parsed JSON of match statistics.
    boost::asio::awaitable<json>
    getMatchStats(std::string_view matchId);

private:
    /// @brief Core GET over TLS, returning parsed JSON and logging HTTP status reasons.
    boost::asio::awaitable<json>
    sendRequest(std::string_view host,
                std::string      target,
                bool             includeAuth);

    /// @brief Build “/path?key=val&…” with URL-encoding.
    static std::string
    buildTarget(std::string_view base,
                std::vector<std::pair<std::string_view,std::string>> const& queries);

    /// @brief Percent-encode per RFC 3986.
    static std::string
    urlEncode(std::string_view value) noexcept;

    std::string               apiKey_;
    boost::asio::io_context   ioContext_;
    boost::asio::ssl::context sslContext_;
};

} // namespace faceit
