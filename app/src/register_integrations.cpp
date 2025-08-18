#include <algorithm>
#include <array>
#include <climits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include <boost/asio/awaitable.hpp>

#include <glaze/json.hpp>

#include <app/register_integrations.hpp>
#include <app/faceit/faceit_client.hpp>
#include <app/faceit/record_service.hpp>

namespace app {
namespace {

// "#chat" -> "chat", ASCII lower
std::string canonical_channel(std::string_view s) {
    if (!s.empty() && s.front() == '#') s.remove_prefix(1);
    std::string out; out.reserve(s.size());
    for (unsigned char c : s)
        out.push_back((c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : char(c));
    return out;
}

} // namespace

using twitch_bot::IrcMessage;

void register_integrations(twitch_bot::TwitchBot& bot,
                           const app::Integrations& integrations,
                           app::AppChannelStore& store)
{
    auto& dispatcher_  = bot.dispatcher();
    auto& helix_ = bot.helix();

    // ------------------------------ FACEIT -----------------------------------
    if (auto key = integrations.api_key_opt("faceit")) {
        auto faceitClient = std::make_shared<faceit::Client>(
            bot.executor(), bot.ssl_context(), *key);

        // !setfaceit <nickname>   (mods/broadcaster only)
        dispatcher_.register_command(
            "setfaceit",
            [&, faceitClient](IrcMessage msg) noexcept -> boost::asio::awaitable<void> {
                const auto channel = msg.params[0];
                const auto parent  = msg.get_tag("id");
                const auto nick_sv = msg.trailing;

                if (!bot.is_privileged(msg)) {
                    co_await bot.reply(channel, parent, "You must be a moderator to use this command");
                    co_return;
                }
                if (nick_sv.empty()) {
                    co_await bot.reply(channel, parent, "Usage: !setfaceit <faceit_nickname>");
                    co_return;
                }

                const std::string chan = canonical_channel(channel);
                const std::string nick{nick_sv};

                store.set_faceit_nick(chan, nick);
                store.clear_faceit_id(chan); // nickname changed -> invalidate cached id
                store.save();

                co_await bot.reply(channel, parent, "FACEIT nickname set to '" + nick + "'");
            });

        // Resolve immutable player_id for a channel (persist in store).
        auto resolve_player_id =
            [&, faceitClient](std::string_view channel)
                -> boost::asio::awaitable<std::optional<std::string>> {
            const std::string chan = canonical_channel(channel);

            if (auto id = store.get_faceit_id(chan); id && !id->empty())
                co_return *id;

            auto nick = store.get_faceit_nick(chan);
            if (!nick) co_return std::nullopt;

            glz::json_t pj;
            try {
                pj = co_await faceitClient->get_player_by_nickname(*nick, "cs2");
            } catch (...) {
                co_return std::nullopt;
            }
            if (!pj.is_object()) co_return std::nullopt;

            std::string id;
            try {
                id = pj.get_object().at("player_id").get_string();
            } catch (...) {
                co_return std::nullopt;
            }

            store.set_faceit_id(chan, id);
            store.save();
            co_return id;
        };

        struct Level { int lvl, min, max; };
        static constexpr std::array<Level, 10> kLevels{{
            {10, 2001, INT_MAX}, {9, 1751, 2000}, {8, 1531, 1750}, {7, 1351, 1530},
            {6, 1201, 1350},     {5, 1051, 1200}, {4, 901, 1050},  {3, 751, 900},
            {2, 501, 750},       {1, 100, 500}
        }};

        // !rank / !elo
        auto rank_handler = [&, faceitClient, resolve_player_id](IrcMessage msg) noexcept
            -> boost::asio::awaitable<void> {
            const auto channel = msg.params[0];
            const auto parent  = msg.get_tag("id");

            auto playerIdOpt = co_await resolve_player_id(channel);
            if (!playerIdOpt) {
                co_await bot.reply(channel, parent, "No FACEIT nickname set");
                co_return;
            }
            const std::string& playerId = *playerIdOpt;

            glz::json_t data;
            bool fetch_ok = true;
            try {
                data = co_await faceitClient->get_player_by_id(playerId);
            } catch (...) {
                fetch_ok = false;
            }
            if (!fetch_ok || !data.is_object()) {
                co_await bot.reply(channel, parent, "Failed to fetch FACEIT data");
                co_return;
            }

            int elo = 0;
            try {
                auto &games = data.get_object().at("games").get_object();
                if (auto it = games.find("cs2"); it != games.end() && it->second.is_object()) {
                    elo = it->second.get_object().at("faceit_elo").as<int>();
                }
            } catch (...) {}

            int level = 1;
            for (const auto& L : kLevels) {
                if (elo >= L.min && elo <= L.max) { level = L.lvl; break; }
            }

            std::ostringstream oss;
            oss << "Level " << level << " | " << elo;
            co_await bot.reply(channel, parent, oss.str());
        };

        dispatcher_.register_command("rank", rank_handler);
        dispatcher_.register_command("elo",  rank_handler);

        // !record [limit]
        dispatcher_.register_command(
            "record",
            [&, faceitClient, resolve_player_id](IrcMessage msg) noexcept
                -> boost::asio::awaitable<void> {
                const auto channel = msg.params[0];
                const auto parent  = msg.get_tag("id");

                auto status_opt = co_await helix_.get_stream_status(channel);
                if (!status_opt || !status_opt->is_live) {
                    co_await bot.reply(channel, parent, "Stream is offline");
                    co_return;
                }
                const auto stream_start = status_opt->start_time;

                int limit = 100;
                if (!msg.trailing.empty()) {
                    try { limit = std::clamp(std::stoi(std::string{msg.trailing}), 1, 100); }
                    catch (...) {}
                }

                auto playerIdOpt = co_await resolve_player_id(channel);
                if (!playerIdOpt) {
                    co_await bot.reply(channel, parent, "No FACEIT nickname set");
                    co_return;
                }
                const std::string& playerId = *playerIdOpt;

                twitch_bot::RecordSummary sum{};
                bool ok = true;
                try {
                    sum = co_await twitch_bot::fetch_record_summary(
                        playerId, stream_start, limit, *faceitClient);
                } catch (...) {
                    ok = false;
                }
                if (!ok) {
                    co_await bot.reply(channel, parent, "Failed to fetch record");
                    co_return;
                }

                std::ostringstream o;
                o << sum.wins << "W/" << sum.losses << "L (" << sum.matchCount << ") | Elo "
                  << sum.currentElo << (sum.eloChange >= 0 ? " (+" : " (") << sum.eloChange << ")";
                co_await bot.reply(channel, parent, o.str());
            });
    }
}

} // namespace app
