// C++ Standard Library
#include <algorithm>
#include <string>

// Project
#include <app/faceit/record_service.hpp>

namespace twitch_bot {

static int tally_wins(const std::vector<glz::json_t>& stats)
{
    int wins = 0;
    for (const auto& m : stats) {
        if (!m.is_object())
            continue;
        const auto& so = m.get_object().at("stats").get_object();
        auto it = so.find("Result");
        if (it != so.end() && it->second.is_string() && it->second.get_string() == "1") {
            ++wins;
        }
    }
    return wins;
}

boost::asio::awaitable<RecordSummary> fetch_record_summary(std::string_view playerId,
                                                           std::chrono::milliseconds since,
                                                           int limit,
                                                           faceit::Client& faceit)
{
    // 1) recent match stats (v4)
    std::vector<glz::json_t> stats
        = co_await faceit.get_player_stats(playerId, since.count(), std::nullopt, limit);
    int wins = tally_wins(stats);
    int losses = int(stats.size()) - wins;

    // 2) ELO history (v1)
    std::vector<glz::json_t> history = co_await faceit.get_elo_history(playerId,
                                                                       /* size= */ limit,
                                                                       /* page= */ 0,
                                                                       /* fromMs= */ since.count(),
                                                                       /* toMs= */ std::nullopt);
    std::sort(history.begin(), history.end(), [](const auto& a, const auto& b) {
        return a.get_object().at("date").as<int64_t>() < b.get_object().at("date").as<int64_t>();
    });

    int eloChange = 0;
    if (history.size() >= 2) {
        const auto& first_entry = history.front().get_object();
        const auto& last_entry = history.back().get_object();
        int first_elo = std::stoi(first_entry.at("elo").get_string());
        int last_elo = std::stoi(last_entry.at("elo").get_string());
        eloChange = last_elo - first_elo;
    }

    // 3) fresh current Elo (v4)
    glz::json_t profile = co_await faceit.get_player_by_id(playerId);
    int currentElo = profile.get_object()
                         .at("games")
                         .get_object()
                         .at("cs2")
                         .get_object()
                         .at("faceit_elo")
                         .as<int>();

    // zero-initialize the struct so every field has a known value
    co_return RecordSummary{wins, losses, int(stats.size()), currentElo, eloChange};
}

} // namespace twitch_bot
