#include "twitch_bot.hpp"
#include "message_parser.hpp"
#include "faceit_client.hpp"

#include <iostream>
#include <sstream>
#include <algorithm>
#include <climits>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/steady_timer.hpp>

namespace twitch_bot {

TwitchBot::TwitchBot(std::string oauthToken,
                     std::string clientId,
                     std::string clientSecret,
                     std::string controlChannel,
                     std::string faceitApiKey)
  : ioc_(),
    ssl_ctx_(boost::asio::ssl::context::tlsv12_client),
    oauthToken_(std::move(oauthToken)),
    clientId_(std::move(clientId)),
    clientSecret_(std::move(clientSecret)),
    controlChannel_(std::move(controlChannel))
{
    ssl_ctx_.set_default_verify_paths();

    // Load channel metadata (aliases + FACEIT nicknames) from disk
    channelStore_ = std::make_unique<ChannelStore>();
    channelStore_->load();

    // Initialise API clients and command dispatcher
    helixClient_   = std::make_unique<HelixClient>(ioc_, ssl_ctx_, clientId_, clientSecret_);
    faceitClient_ = std::make_unique<faceit::Client>(std::move(faceitApiKey));
    dispatcher_   = std::make_unique<CommandDispatcher>();

    //
    // Register chat commands
    //

    // -- !setnickname <alias>
    dispatcher_->registerCommand("!setnickname",
        [this](std::string_view channel, std::string_view, std::string_view args)
            -> boost::asio::awaitable<void>
        {
            const std::string alias = std::string(args);
            channelStore_->setAlias(channel, alias);
            channelStore_->save();

            co_await ircClient_->sendLine(
                "PRIVMSG #" + std::string(channel) + " :Alias set to " + alias
            );
            co_return;
        });

    // -- !setfaceit <faceitNick>
    dispatcher_->registerCommand("!setfaceit",
        [this](std::string_view channel, std::string_view, std::string_view args)
            -> boost::asio::awaitable<void>
        {
            const std::string faceitNick = std::string(args);
            channelStore_->setFaceitNick(channel, faceitNick);
            channelStore_->save();

            co_await ircClient_->sendLine(
                "PRIVMSG #" + std::string(channel) + " :FACEIT nickname set to " + faceitNick
            );
            co_return;
        });

    // -- !join <channel>  (only allowed in controlChannel_)
    dispatcher_->registerCommand("!join",
        [this](std::string_view channel, std::string_view user, std::string_view args)
            -> boost::asio::awaitable<void>
        {
            if (channel != controlChannel_)
                co_return;

            const std::string newChan = !args.empty()
                ? std::string(args)
                : std::string(user);

            channelStore_->addChannel(newChan);
            channelStore_->save();

            co_await ircClient_->sendLine("JOIN #" + newChan);
            co_await ircClient_->sendLine(
                "PRIVMSG #" + controlChannel_ + " :Joined " + newChan
            );
            co_return;
        });

    // -- !leave <channel>  (only allowed in controlChannel_)
    dispatcher_->registerCommand("!leave",
        [this](std::string_view channel, std::string_view user, std::string_view args)
            -> boost::asio::awaitable<void>
        {
            if (channel != controlChannel_)
                co_return;

            const std::string remChan = !args.empty()
                ? std::string(args)
                : std::string(user);

            channelStore_->removeChannel(remChan);
            channelStore_->save();

            co_await ircClient_->sendLine("PART #" + remChan);
            co_await ircClient_->sendLine(
                "PRIVMSG #" + controlChannel_ + " :Left " + remChan
            );
            co_return;
        });

    // -- !rank (alias: !elo)
    auto rankHandler = [this](std::string_view channel,
                              std::string_view,
                              std::string_view)
        -> boost::asio::awaitable<void>
    {
        // Ensure FACEIT nickname is set for this channel
        const auto optFaceit = channelStore_->getFaceitNick(channel);
        if (!optFaceit) {
            co_await ircClient_->sendLine(
                "PRIVMSG #" + std::string(channel)
                + " :No FACEIT nickname set. Use !setfaceit first."
            );
            co_return;
        }
        const std::string faceitNick = *optFaceit;

        // Query FACEIT for player data
        bool fetchOk = true;
        boost::json::value playerVal;
        try {
            playerVal = co_await faceitClient_->getPlayerByNickname(faceitNick, "cs2");
        } catch (...) {
            fetchOk = false;
        }

        if (!fetchOk) {
            co_await ircClient_->sendLine(
                "PRIVMSG #" + std::string(channel) + " :Failed to fetch FACEIT rank"
            );
            co_return;
        }

        // Extract current Elo and compute level
        const auto& playerObj = playerVal.as_object();
        const int currentElo = playerObj
            .at("games").as_object()
            .at("cs2").as_object()
            .at("faceit_elo").to_number<int>();

        struct LevelInfo { int level, minElo, maxElo; };
        static constexpr LevelInfo levels[] = {
            {10, 2001, INT_MAX}, {9, 1751, 2000}, {8, 1531, 1750},
            {7, 1351, 1530},     {6, 1201, 1350},     {5, 1051, 1200},
            {4,  901, 1050},     {3,  751,  900},     {2,  501,  750},
            {1,  100,  500}
        };

        int level = 1;
        for (const auto& info : levels) {
            if (currentElo >= info.minElo && currentElo <= info.maxElo) {
                level = info.level;
                break;
            }
        }

        // Use alias if set; otherwise channel name
        const std::string displayName = channelStore_->getAlias(channel)
            .value_or(std::string(channel));

        std::ostringstream oss;
        oss << "PRIVMSG #" << channel << " :"
            << displayName << " is level " << level
            << " | " << currentElo << " Elo";

        co_await ircClient_->sendLine(oss.str());
        co_return;
    };

    dispatcher_->registerCommand("!rank", rankHandler);
    dispatcher_->registerCommand("!elo",  rankHandler);

    // -- !record [limit]
    dispatcher_->registerCommand("!record",
        [this](std::string_view channel, std::string_view, std::string_view args)
            -> boost::asio::awaitable<void>
        {
            // Check if the stream is currently live
            const auto streamStartOpt = co_await helixClient_->getStreamStart(channel);
            if (!streamStartOpt) {
                co_await ircClient_->sendLine(
                    "PRIVMSG #" + std::string(channel) + " :Stream is offline"
                );
                co_return;
            }

            // Ensure FACEIT nickname is set
            const auto optFaceit = channelStore_->getFaceitNick(channel);
            if (!optFaceit) {
                co_await ircClient_->sendLine(
                    "PRIVMSG #" + std::string(channel)
                    + " :No FACEIT nickname set. Use !setfaceit first."
                );
                co_return;
            }
            const std::string faceitNick = *optFaceit;

            // Parse optional 'limit' parameter in [1,100], default = 100
            int limit = 100;
            if (!args.empty()) {
                try {
                    limit = std::clamp(std::stoi(std::string(args)), 1, 100);
                } catch (...) {
                    limit = 100;
                }
            }

            // Fetch player info from FACEIT
            bool fetchOk = true;
            boost::json::value playerVal;
            try {
                playerVal = co_await faceitClient_->getPlayerByNickname(faceitNick, "cs2");
            } catch (...) {
                fetchOk = false;
            }
            if (!fetchOk) {
                co_await ircClient_->sendLine(
                    "PRIVMSG #" + std::string(channel)
                    + " :Failed to fetch FACEIT stats"
                );
                co_return;
            }

            const auto& playerObj = playerVal.as_object();
            const std::string pid = std::string(playerObj.at("player_id").as_string());

            // Calculate how long the stream has been live (in ms)
            const auto sinceMs = streamStartOpt->started_at.count();

            // Fetch recent matches since stream start
            bool statsOk = true;
            std::vector<boost::json::value> statsArr;
            try {
                statsArr = co_await faceitClient_->getPlayerStats(pid, sinceMs, std::nullopt, limit);
            } catch (...) {
                statsOk = false;
            }
            if (!statsOk) {
                co_await ircClient_->sendLine(
                    "PRIVMSG #" + std::string(channel)
                    + " :Failed to fetch match stats"
                );
                co_return;
            }

            // Count wins and losses
            const int wins = static_cast<int>(
                std::count_if(
                    statsArr.begin(),
                    statsArr.end(),
                    [](auto const& match) {
                        return match.as_object()
                            .at("stats").as_object()
                            .at("Result").as_string() == "1";
                    }
                )
            );
            const int losses = static_cast<int>(statsArr.size()) - wins;

            // Fetch Elo history to compute Elo change
            bool historyOk = true;
            std::vector<boost::json::value> historyArr;
            try {
                historyArr = co_await faceitClient_->getEloHistory(pid, limit, 0, sinceMs, std::nullopt);
            } catch (...) {
                historyOk = false;
            }
            if (!historyOk) {
                co_await ircClient_->sendLine(
                    "PRIVMSG #" + std::string(channel)
                    + " :Failed to fetch Elo history"
                );
                co_return;
            }

            // Sort history by date (ascending)
            std::sort(
                historyArr.begin(),
                historyArr.end(),
                [](auto const& a, auto const& b) {
                    return a.as_object().at("date").to_number<int64_t>()
                         < b.as_object().at("date").to_number<int64_t>();
                }
            );

            // Compute Elo change over the stream period
            int eloChange = 0;
            if (historyArr.size() >= 2) {
                const auto& first = historyArr.front().as_object();
                const auto& last  = historyArr.back().as_object();
                const int firstElo = std::stoi(std::string(first.at("elo").as_string()));
                const int lastElo  = std::stoi(std::string(last.at("elo").as_string()));
                eloChange = lastElo - firstElo;
            }

            // Extract current Elo from player object
            const int currentElo = playerObj
                .at("games").as_object()
                .at("cs2").as_object()
                .at("faceit_elo").to_number<int>();

            // Build and send the summary message
            std::ostringstream oss;
            oss << "PRIVMSG #" << channel << " :"
                << wins << "W/" << losses << "L ("
                << statsArr.size() << ") | Elo " << currentElo
                << (eloChange >= 0 ? " (+" : " (") << eloChange << ")";
            co_await ircClient_->sendLine(oss.str());
            co_return;
        });

    // Finally, initialise the IRC client
    ircClient_ = std::make_unique<IrcClient>(ioc_, ssl_ctx_, oauthToken_, controlChannel_);
}

TwitchBot::~TwitchBot() noexcept {
    if (ircClient_) {
        ircClient_->close();
    }
    ioc_.stop();
}

void TwitchBot::addChatListener(ChatListener cb) {
    dispatcher_->registerChatListener(std::move(cb));
}

void TwitchBot::run() {
    // Launch the main bot coroutine
    boost::asio::co_spawn(
        ioc_,
        [this]() -> boost::asio::awaitable<void> {
            co_await runBot();
        },
        boost::asio::detached
    );

    // Run the I/O loop until explicitly stopped
    ioc_.run();
}

boost::asio::awaitable<void> TwitchBot::runBot() {
    // Ensure controlChannel_ is always in the join list
    auto channels = channelStore_->allChannels();
    if (std::find(channels.begin(), channels.end(), controlChannel_) == channels.end()) {
        channels.push_back(controlChannel_);
    }

    // Connect to Twitch IRC and join channels
    try {
        co_await ircClient_->connect(channels);
    } catch (const std::exception& e) {
        std::cerr << "[TwitchBot] connect error: " << e.what() << '\n';
        co_return;
    }

    const auto executor = co_await boost::asio::this_coro::executor;

    // Spawn a background ping loop to keep the connection alive
    boost::asio::co_spawn(
        executor,
        [this]() -> boost::asio::awaitable<void> {
            co_await ircClient_->pingLoop();
        },
        boost::asio::detached
    );

    // Spawn a background read loop to process incoming IRC messages
    boost::asio::co_spawn(
        executor,
        [this]() -> boost::asio::awaitable<void> {
            co_await ircClient_->readLoop(
                [this](std::string_view rawLine) {
                    // Log raw IRC data for debugging
                    std::cout << "[IRC] " << rawLine << '\n';
                    const auto msg = parseIrcLine(rawLine);
                    boost::asio::co_spawn(
                        ioc_,
                        dispatcher_->dispatch(msg),
                        boost::asio::detached
                    );
                }
            );
        },
        boost::asio::detached
    );

    // Prevent runBot() from returning by idling indefinitely
    boost::asio::steady_timer idle{ioc_};
    idle.expires_at(std::chrono::steady_clock::time_point::max());
    co_await idle.async_wait(boost::asio::use_awaitable);
}

} // namespace twitch_bot
