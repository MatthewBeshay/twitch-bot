#pragma once

// C++ Standard Library
#include <chrono>
#include <string>
#include <vector>

// 3rd-party
#include <boost/asio/awaitable.hpp>
#include <glaze/json.hpp>

// Project
#include "faceit_client.hpp"

namespace twitch_bot {

/// The minimal data we want to send back to IRC.
struct RecordSummary {
    int wins;
    int losses;
    int matchCount;
    int currentElo;
    int eloChange;
};

/// Fetch all of the FACEIT data needed for “!record” and compute the summary.
/// @param playerId  immutable FACEIT ID
/// @param since     stream-start time
/// @param limit     max matches to fetch
/// @param faceit    your HTTP client
/// @throws on any HTTP or parse failure
boost::asio::awaitable<RecordSummary> fetch_record_summary(std::string_view playerId,
                                                           std::chrono::milliseconds since,
                                                           int limit,
                                                           faceit::Client& faceit);

} // namespace twitch_bot
