// C++ Standard Library
#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <iostream>
#include <span>
#include <sstream>

// 3rd-party
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>

// Project
#include "message_parser.hpp"
#include "record_service.hpp"
#include "twitch_bot.hpp"

namespace twitch_bot {

TwitchBot::TwitchBot(std::string oauth_token,
                     std::string client_id,
                     std::string client_secret,
                     std::string control_channel,
                     std::string faceit_api_key,
                     std::size_t threads)
    : pool_{threads > 0 ? threads : 1} // I/O thread-pool
    , strand_{pool_.get_executor()} // serialises all work
    , ssl_ctx_{boost::asio::ssl::context::tlsv12_client}
    , oauth_token_{std::move(oauth_token)}
    , client_id_{std::move(client_id)}
    , client_secret_{std::move(client_secret)}
    , control_channel_{std::move(control_channel)}
    , faceit_api_key_{std::move(faceit_api_key)}
    , irc_client_{strand_, ssl_ctx_, oauth_token_, control_channel_}
    , dispatcher_{strand_}
    , helix_client_{strand_, ssl_ctx_, client_id_, client_secret_}
    , channel_store_{strand_}
    , faceit_client_{strand_, ssl_ctx_, faceit_api_key_}
{
    ssl_ctx_.set_default_verify_paths();
    channel_store_.load();

    // ---------- !join ---------------------------------------------------------
    dispatcher_.register_command(
        "join", [this](IrcMessage msg) noexcept -> boost::asio::awaitable<void> {
            auto channel = msg.params[0];
            auto user = msg.prefix;
            auto args = msg.trailing;

            if (channel != control_channel_) // only from control channel
                co_return;

            // mods may target another channel
            if (!args.empty() && !isPrivileged(msg)) {
                std::array<boost::asio::const_buffer, 6> warn{
                    {boost::asio::buffer("PRIVMSG #", 9), boost::asio::buffer(control_channel_),
                     boost::asio::buffer(" :@", 3), boost::asio::buffer(user),
                     boost::asio::buffer(
                         " You must be a mod to invite the bot to a different channel. "
                         "Use !join from your own channel instead.",
                         118),
                     boost::asio::buffer(kCRLF)}};
                co_await irc_client_.send_buffers(warn);
                co_return;
            }

            std::string_view target = args.empty() ? user : args;

            // ignore duplicates
            if (channel_store_.contains(target)) {
                std::array<boost::asio::const_buffer, 5> msg_exist{
                    {boost::asio::buffer("PRIVMSG #", 9), boost::asio::buffer(control_channel_),
                     boost::asio::buffer(" :Already in channel ", 22), boost::asio::buffer(target),
                     boost::asio::buffer(kCRLF)}};
                co_await irc_client_.send_buffers(msg_exist);
                co_return;
            }

            channel_store_.add_channel(target);
            channel_store_.save();

            // JOIN #<target>
            std::array<boost::asio::const_buffer, 3> join_cmd{{boost::asio::buffer("JOIN #", 6),
                                                               boost::asio::buffer(target),
                                                               boost::asio::buffer(kCRLF)}};
            co_await irc_client_.send_buffers(join_cmd);

            // acknowledgement
            std::array<boost::asio::const_buffer, 7> ack{
                {boost::asio::buffer("PRIVMSG #", 9), boost::asio::buffer(control_channel_),
                 boost::asio::buffer(" :@", 3), boost::asio::buffer(user),
                 boost::asio::buffer(" Joined ", 8), boost::asio::buffer(target),
                 boost::asio::buffer(kCRLF)}};
            co_await irc_client_.send_buffers(ack);
        });

    // ---------- !leave --------------------------------------------------------
    dispatcher_.register_command(
        "leave", [this](IrcMessage msg) noexcept -> boost::asio::awaitable<void> {
            auto channel = msg.params[0];
            auto user = msg.prefix;
            auto args = msg.trailing;

            if (channel != control_channel_)
                co_return;

            // mods may target another channel
            if (!args.empty() && !isPrivileged(msg)) {
                std::array<boost::asio::const_buffer, 6> warn{
                    {boost::asio::buffer("PRIVMSG #", 9), boost::asio::buffer(control_channel_),
                     boost::asio::buffer(" :@", 3), boost::asio::buffer(user),
                     boost::asio::buffer(
                         " You must be a mod to remove the bot from another channel. "
                         "Use !leave from your own channel instead.",
                         119),
                     boost::asio::buffer(kCRLF)}};
                co_await irc_client_.send_buffers(warn);
                co_return;
            }

            std::string_view target = args.empty() ? user : args;

            if (!channel_store_.contains(target)) {
                std::array<boost::asio::const_buffer, 5> msg_absent{
                    {boost::asio::buffer("PRIVMSG #", 9), boost::asio::buffer(control_channel_),
                     boost::asio::buffer(" :Not in channel ", 19), boost::asio::buffer(target),
                     boost::asio::buffer(kCRLF)}};
                co_await irc_client_.send_buffers(msg_absent);
                co_return;
            }

            channel_store_.remove_channel(target);
            channel_store_.save();

            // PART #<target>
            std::array<boost::asio::const_buffer, 3> part_cmd{{boost::asio::buffer("PART #", 6),
                                                               boost::asio::buffer(target),
                                                               boost::asio::buffer(kCRLF)}};
            co_await irc_client_.send_buffers(part_cmd);

            std::array<boost::asio::const_buffer, 7> ack{
                {boost::asio::buffer("PRIVMSG #", 9), boost::asio::buffer(control_channel_),
                 boost::asio::buffer(" :@", 3), boost::asio::buffer(user),
                 boost::asio::buffer(" Left ", 6), boost::asio::buffer(target),
                 boost::asio::buffer(kCRLF)}};
            co_await irc_client_.send_buffers(ack);
        });

    // ---------- !channels -----------------------------------------------------
    dispatcher_.register_command(
        "channels", [this](IrcMessage msg) noexcept -> boost::asio::awaitable<void> {
            auto channel = msg.params[0];
            if (channel != control_channel_)
                co_return;

            std::vector<std::string_view> names;
            channel_store_.channel_names(names);

            std::string list;
            for (std::size_t i = 0; i < names.size(); ++i) {
                list += names[i];
                if (i + 1 < names.size())
                    list += ", ";
            }
            if (list.empty())
                list = "(none)";

            auto reply = std::format(
                "PRIVMSG #{} :Currently in channels: {}{}", control_channel_, list, kCRLF);
            co_await irc_client_.send_line(reply);
        });

    // ---------- !setnickname --------------------------------------------------
    dispatcher_.register_command(
        "setnickname", [this](IrcMessage msg) noexcept -> boost::asio::awaitable<void> {
            auto channel = msg.params[0];
            auto alias = msg.trailing;

            if (!isPrivileged(msg)) {
                // inform them they need mod
                std::ostringstream err;
                err << "PRIVMSG #" << channel << " :You must be a moderator to use this command"
                    << kCRLF;
                co_await irc_client_.send_line(err.str());
                co_return;
            }

            channel_store_.set_alias(channel, std::string{alias});
            channel_store_.save();

            std::ostringstream ok;
            ok << "PRIVMSG #" << channel << " :Alias set to '" << alias << "'" << kCRLF;
            co_await irc_client_.send_line(ok.str());
        });

    // ---------- !setfaceit ----------------------------------------------------
    dispatcher_.register_command(
        "setfaceit", [this](IrcMessage msg) noexcept -> boost::asio::awaitable<void> {
            auto channel = msg.params[0];
            auto faceit_nick = msg.trailing;

            if (!isPrivileged(msg)) {
                std::ostringstream err;
                err << "PRIVMSG #" << channel << " :You must be a moderator to use this command"
                    << kCRLF;
                co_await irc_client_.send_line(err.str());
                co_return;
            }

            channel_store_.set_faceit_nick(channel, std::string{faceit_nick});
            channel_store_.save();

            std::ostringstream ok;
            ok << "PRIVMSG #" << channel << " :FACEIT nickname set to '" << faceit_nick << "'"
               << kCRLF;
            co_await irc_client_.send_line(ok.str());
        });

    // ---------- !rank / !elo --------------------------------------------------
    auto rank_handler = [this](IrcMessage msg) noexcept -> boost::asio::awaitable<void> {
        auto channel = msg.params[0];

        // 1) Resolve immutable player-ID
        std::string playerId;
        if (auto opt = channel_store_.get_faceit_id(channel)) {
            playerId = std::string{*opt};
        } else if (auto nick = channel_store_.get_faceit_nick(channel)) {
            // first­-time lookup
            glz::json_t pj = co_await faceit_client_.get_player_by_nickname(*nick, "cs2");
            playerId = pj.get_object().at("player_id").get_string();
            channel_store_.set_faceit_id(channel, playerId);
        } else {
            std::ostringstream oss;
            oss << "PRIVMSG #" << channel << " :No FACEIT nickname set" << kCRLF;
            co_await irc_client_.send_line(oss.str());
            co_return;
        }

        // 2) Fetch current player data
        glz::json_t data;
        bool fetch_ok = true;
        try {
            data = co_await faceit_client_.get_player_by_id(playerId);
        } catch (...) {
            fetch_ok = false;
        }
        if (!fetch_ok) {
            std::ostringstream oss;
            oss << "PRIVMSG #" << channel << " :Failed to fetch FACEIT data" << kCRLF;
            co_await irc_client_.send_line(oss.str());
            co_return;
        }

        // 3) Extract Elo
        int elo = 0;
        if (data.is_object()) {
            auto &games = data.get_object().at("games").get_object();
            if (auto it = games.find("cs2"); it != games.end() && it->second.is_object()) {
                elo = it->second.get_object().at("faceit_elo").as<int>();
            }
        }

        // 4) Map Elo to level
        struct Level {
            int lvl, min, max;
        };
        static constexpr std::array<Level, 10> levels{{{10, 2001, INT_MAX},
                                                       {9, 1751, 2000},
                                                       {8, 1531, 1750},
                                                       {7, 1351, 1530},
                                                       {6, 1201, 1350},
                                                       {5, 1051, 1200},
                                                       {4, 901, 1050},
                                                       {3, 751, 900},
                                                       {2, 501, 750},
                                                       {1, 100, 500}}};
        int level = 1;
        for (const auto &L : levels) {
            if (elo >= L.min && elo <= L.max) {
                level = L.lvl;
                break;
            }
        }

        // 5) Send reply
        {
            std::ostringstream oss;
            oss << "PRIVMSG #" << channel << " :" << "Level " << level << " | " << elo << kCRLF;
            co_await irc_client_.send_line(oss.str());
        }
    };

    dispatcher_.register_command("rank", rank_handler);
    dispatcher_.register_command("elo", rank_handler);

    // ---------- !record -------------------------------------------------------
    dispatcher_.register_command(
        "record", [this](IrcMessage msg) noexcept -> boost::asio::awaitable<void> {
            auto channel = msg.params[0];

            // 1) Check stream state
            auto status_opt = co_await helix_client_.get_stream_status(channel);
            if (!status_opt || !status_opt->is_live) {
                std::ostringstream o;
                o << "PRIVMSG #" << channel << " :Stream is offline" << kCRLF;
                co_await irc_client_.send_line(o.str());
                co_return;
            }
            auto stream_start = status_opt->start_time;

            // 2) Parse optional limit argument
            int limit = 100;
            if (!msg.trailing.empty()) {
                try {
                    limit = std::clamp(std::stoi(std::string{msg.trailing}), 1, 100);
                } catch (...) {
                    // leave default
                }
            }

            // 3) Resolve (or cache) the immutable FACEIT player ID
            std::string playerId;
            if (auto pid = channel_store_.get_faceit_id(channel)) {
                playerId = *pid;
            } else if (auto nick = channel_store_.get_faceit_nick(channel)) {
                // first-time lookup
                glz::json_t pj = co_await faceit_client_.get_player_by_nickname(*nick, "cs2");
                playerId = pj.get_object().at("player_id").get_string();
                channel_store_.set_faceit_id(channel, playerId);
                channel_store_.save();
            } else {
                std::ostringstream o;
                o << "PRIVMSG #" << channel << " :No FACEIT nickname set" << kCRLF;
                co_await irc_client_.send_line(o.str());
                co_return;
            }

            // 4) Delegate to record_service
            RecordSummary sum;
            bool ok = true;
            try {
                sum = co_await fetch_record_summary(playerId, stream_start, limit, faceit_client_);
            } catch (...) {
                ok = false;
            }
            if (!ok) {
                std::ostringstream o;
                o << "PRIVMSG #" << channel << " :Failed to fetch record" << kCRLF;
                co_await irc_client_.send_line(o.str());
                co_return;
            }

            // 5) Format + send
            std::ostringstream o;
            o << "PRIVMSG #" << channel << " :" << sum.wins << "W/" << sum.losses << "L ("
              << sum.matchCount << ") | Elo " << sum.currentElo
              << (sum.eloChange >= 0 ? " (+" : " (") << sum.eloChange << ")" << kCRLF;
            co_await irc_client_.send_line(o.str());
        });
}

TwitchBot::~TwitchBot() noexcept
{
    irc_client_.close();
}

void TwitchBot::add_chat_listener(chat_listener_t listener)
{
    dispatcher_.register_chat_listener(std::move(listener));
}

void TwitchBot::run()
{
    boost::asio::co_spawn(strand_, run_bot(), boost::asio::detached);
    pool_.join(); // block until stop
}

boost::asio::awaitable<void> TwitchBot::run_bot() noexcept
{
    // Ensure bot is in its own control channel
    std::vector<std::string_view> channels;
    channel_store_.channel_names(channels);
    if (std::find(channels.begin(), channels.end(), control_channel_) == channels.end())
        channels.push_back(control_channel_);

    try {
        co_await irc_client_.connect(channels);
    } catch (const std::exception &e) {
        std::cerr << "[TwitchBot] connect error: " << e.what() << '\n';
        co_return;
    }

    auto exec = co_await boost::asio::this_coro::executor;

    // keep-alive PING
    boost::asio::co_spawn(
        exec,
        [this]() noexcept -> boost::asio::awaitable<void> { co_await irc_client_.ping_loop(); },
        boost::asio::detached);

    // read / dispatch
    boost::asio::co_spawn(
        exec,
        [this]() noexcept -> boost::asio::awaitable<void> {
            co_await irc_client_.read_loop([this](std::string_view raw) {
                std::cout << "[IRC] " << raw << '\n';
                dispatcher_.dispatch(parse_irc_line(raw));
            });
        },
        boost::asio::detached);

    // idle indefinitely
    boost::asio::steady_timer idle{pool_};
    idle.expires_at(std::chrono::steady_clock::time_point::max());
    co_await idle.async_wait(boost::asio::use_awaitable);
}

} // namespace twitch_bot
