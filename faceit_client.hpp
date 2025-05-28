// faceit_client.hpp
#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

namespace faceit {

    /// High-performance client for FACEIT’s v4/v1 Data APIs over HTTPS.
    ///
    /// Throws on network errors or unexpected response formats.
    class Client {
    public:
        /// @param api_key  Non-empty FACEIT API key (v4). Throws std::invalid_argument if empty.
        explicit Client(std::string api_key);

        /// @brief Lookup a player by nickname (v4).
        /// @param nickname  Player’s display name.
        /// @param game      Game slug (default "cs2").
        /// @returns JSON object of player details.
        nlohmann::json get_player_by_nickname(std::string_view nickname,
            std::string_view game = "cs2");

        /// @brief Lookup a player by FACEIT ID (v4).
        /// @param player_id  FACEIT player UUID.
        /// @returns JSON object of player details.
        nlohmann::json get_player_by_id(std::string_view player_id);

        /// @brief Fetch per-match stats for a player (v4).
        /// @param player_id  FACEIT player UUID.
        /// @param from_ts    UNIX timestamp (inclusive).
        /// @param to_ts      Optional UNIX timestamp (exclusive).
        /// @param limit      Max number of entries (default 100).
        /// @returns Vector of per-match JSON stats.
        std::vector<nlohmann::json>
            get_player_stats(std::string_view           player_id,
                int64_t                   from_ts,
                std::optional<int64_t>    to_ts = std::nullopt,
                int                       limit = 100);

        /// @brief Fetch ELO history (v1, public).
        /// @param player_id  FACEIT player UUID.
        /// @param size       Entries per page.
        /// @param page       Page index (0-based).
        /// @param from_ts    Optional UNIX timestamp (inclusive).
        /// @param to_ts      Optional UNIX timestamp (exclusive).
        /// @returns Vector of JSON entries (may be at top level or under `"items"`).
        std::vector<nlohmann::json>
            get_elo_history(std::string_view           player_id,
                int                        size,
                int                        page,
                std::optional<int64_t>     from_ts = std::nullopt,
                std::optional<int64_t>     to_ts = std::nullopt);

        /// @brief Fetch detailed match stats (v4).
        /// @param match_id  FACEIT match UUID.
        /// @returns JSON object of match statistics.
        nlohmann::json get_match_stats(std::string_view match_id);

    private:
        // Build “/path?key=val&…” with URL-encoding.
        static std::string build_target(std::string_view base,
            std::vector<std::pair<std::string_view, std::string>> const& queries);

        // Percent-encode per RFC 3986.
        static std::string url_encode(std::string_view value) noexcept;

        // Core request: GET over TLS, parse JSON.
        nlohmann::json send_request(std::string_view host,
            std::string      target,
            bool             include_auth);

        std::string                 api_key_;
        boost::asio::io_context     ioc_;
        boost::asio::ssl::context   ssl_ctx_;
    };

} // namespace faceit
