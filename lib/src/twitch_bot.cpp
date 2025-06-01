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
  : ioc_()
  , ssl_ctx_(boost::asio::ssl::context::tlsv12_client)
  , oauthToken_(std::move(oauthToken))
  , clientId_(std::move(clientId))
  , clientSecret_(std::move(clientSecret))
  , controlChannel_(std::move(controlChannel))
{
    ssl_ctx_.set_default_verify_paths();

    // (1) Load per-channel info (alias + faceit) from disk
    channelStore_ = std::make_unique<ChannelStore>();
    channelStore_->load();

    // (2) Create HelixClient (for stream checks)
    helixClient_ = std::make_unique<HelixClient>(ioc_, ssl_ctx_, clientId_, clientSecret_);

    // (3) Create FACEIT client
    faceitClient_ = std::make_unique<faceit::Client>(std::move(faceitApiKey));

    // (4) Create CommandDispatcher
    dispatcher_ = std::make_unique<CommandDispatcher>();

    //
    // Register built-in commands
    //

    // -- setnickname <alias>
    dispatcher_->registerCommand("!setnickname",
      [this](std::string_view channel, std::string_view user, std::string_view args)
        -> boost::asio::awaitable<void>
      {
          std::string alias = std::string(args);
          channelStore_->setAlias(channel, alias);
          channelStore_->save();
          co_await ircClient_->sendLine(
            "PRIVMSG #" + std::string(channel)
            + " :Alias set to " + alias
          );
        co_return;
      });

    // -- setfaceit <faceitNick>
    dispatcher_->registerCommand("!setfaceit",
      [this](std::string_view channel, std::string_view user, std::string_view args)
        -> boost::asio::awaitable<void>
      {
          std::string faceitNick = std::string(args);
          channelStore_->setFaceitNick(channel, faceitNick);
          channelStore_->save();
          co_await ircClient_->sendLine(
            "PRIVMSG #" + std::string(channel)
            + " :FACEIT nickname set to " + faceitNick
          );
        co_return;
      });

    // -- join <newChannel>
    dispatcher_->registerCommand("!join",
      [this](std::string_view channel, std::string_view user, std::string_view args)
        -> boost::asio::awaitable<void>
      {
        if (std::string(channel) == controlChannel_) {
          std::string newChan = std::string(user);

          channelStore_->addChannel(newChan);
          channelStore_->save();

          co_await ircClient_->sendLine("JOIN #" + newChan);

          co_await ircClient_->sendLine(
            "PRIVMSG #" + controlChannel_ + " :Joined " + std::string(user)
          );
        }
        else {
          std::cout << "[DEBUG] !join ignored: request not from controlChannel (" 
                    << channel << " != " << controlChannel_ << ")\n";
        }

        co_return;
      });

    // -- leave <removeChannel>
    dispatcher_->registerCommand("!leave",
      [this](std::string_view channel, std::string_view user, std::string_view args)
        -> boost::asio::awaitable<void>
      {
        if (std::string(channel) == controlChannel_) {
          std::string remChan = std::string(user);
          channelStore_->removeChannel(remChan);
          channelStore_->save();
          co_await ircClient_->sendLine("PART #" + remChan);
          co_await ircClient_->sendLine(
            "PRIVMSG #" + controlChannel_ + " :Left " + remChan
          );
        }
        co_return;
      });

    // -- rank
    dispatcher_->registerCommand("!rank",
      [this](std::string_view channel, std::string_view user, std::string_view args)
        -> boost::asio::awaitable<void>
      {
        // 1) Retrieve stored FACEIT nick for this channel
        auto optFaceit = channelStore_->getFaceitNick(channel);
        if (!optFaceit) {
          co_await ircClient_->sendLine(
            "PRIVMSG #" + std::string(channel)
            + " :No FACEIT nickname set. Use !setfaceit first."
          );
          co_return;
        }
        const std::string faceitNick = *optFaceit;

        // 2) Query FACEIT for that nick (catch exceptions without co_await inside catch)
        bool fetchOk = true;
        boost::json::value playerVal;
        try {
          playerVal = co_await faceitClient_->getPlayerByNickname(faceitNick, "cs2");
        }
        catch (...) {
          fetchOk = false;
        }
        if (!fetchOk) {
          co_await ircClient_->sendLine(
            "PRIVMSG #" + std::string(channel)
            + " :Failed to fetch FACEIT rank"
          );
          co_return;
        }

        // 3) Extract Elo and compute level
        auto& playerObj = playerVal.as_object();
        int currentElo = playerObj
          .at("games").as_object()
          .at("cs2").as_object()
          .at("faceit_elo").to_number<int>();

        struct LevelInfo { int level, minElo, maxElo; };
        static constexpr LevelInfo levels[] = {
          {10,2001,INT_MAX},{9,1751,2000},{8,1531,1750},
          {7,1351,1530},{6,1201,1350},{5,1051,1200},
          {4, 901,1050},{3, 751, 900},{2, 501, 750},{1, 100, 500}
        };
        int lvl = 1;
        for (auto const& info : levels) {
          if (currentElo >= info.minElo && currentElo <= info.maxElo) {
            lvl = info.level;
            break;
          }
        }

        // 4) Send response, using alias if set, otherwise the raw channel name
        std::string displayName = channelStore_->getAlias(channel).value_or(std::string(channel));

        std::ostringstream oss;
        oss << "PRIVMSG #" << channel
            << " :" << displayName
            << " is level " << lvl
            << " | " << currentElo << " Elo";

        co_await ircClient_->sendLine(oss.str());
        co_return;
      });

    // -- record [limit]
    dispatcher_->registerCommand("!record",
      [this](std::string_view channel, std::string_view user, std::string_view args)
        -> boost::asio::awaitable<void>
      {
        // 1) Check if stream is live
        auto streamStartOpt = co_await helixClient_->getStreamStart(channel);
        if (!streamStartOpt) {
          co_await ircClient_->sendLine(
            "PRIVMSG #" + std::string(channel)
            + " :Stream is offline"
          );
          co_return;
        }

        // 2) Retrieve stored FACEIT nick for this channel
        auto optFaceit = channelStore_->getFaceitNick(channel);
        if (!optFaceit) {
          co_await ircClient_->sendLine(
            "PRIVMSG #" + std::string(channel)
            + " :No FACEIT nickname set. Use !setfaceit first."
          );
          co_return;
        }
        const std::string faceitNick = *optFaceit;

        // 3) Parse optional 'limit' (default = 100)
        int limit = 100;
        if (!args.empty()) {
          try {
            limit = std::clamp(std::stoi(std::string(args)), 1, 100);
          }
          catch (...) {
            limit = 100;
          }
        }

        // 4) Fetch FACEIT player object
        bool fetchOk = true;
        boost::json::value playerVal;
        try {
          playerVal = co_await faceitClient_->getPlayerByNickname(faceitNick, "cs2");
        }
        catch (...) {
          fetchOk = false;
        }
        if (!fetchOk) {
          co_await ircClient_->sendLine(
            "PRIVMSG #" + std::string(channel)
            + " :Failed to fetch FACEIT stats"
          );
          co_return;
        }

        // 5) Extract playerId
        auto& playerObj = playerVal.as_object();
        std::string pid = std::string(playerObj.at("player_id").as_string());

        // 6) Compute ms since stream started
        auto sinceMs = streamStartOpt->started_at.count();

        // 7) Fetch recent matches since streamStart
        bool statsOk = true;
        std::vector<boost::json::value> statsArr;
        try {
          statsArr = co_await faceitClient_->getPlayerStats(pid, sinceMs, std::nullopt, limit);
        }
        catch (...) {
          statsOk = false;
        }
        if (!statsOk) {
          co_await ircClient_->sendLine(
            "PRIVMSG #" + std::string(channel)
            + " :Failed to fetch match stats"
          );
          co_return;
        }

        // Count wins/losses
        int wins = static_cast<int>(
            std::count_if(
                statsArr.begin(),
                statsArr.end(),
                [](auto const& m) {
                    return m.as_object()
                        .at("stats").as_object()
                        .at("Result").as_string() == "1";
                }
            )
            );
        int losses = static_cast<int>(statsArr.size()) - wins;

        // 8) Fetch Elo history (for delta)
        bool historyOk = true;
        std::vector<boost::json::value> historyArr;
        try {
          historyArr = co_await faceitClient_->getEloHistory(pid, limit, 0, sinceMs, std::nullopt);
        }
        catch (...) {
          historyOk = false;
        }
        if (!historyOk) {
          co_await ircClient_->sendLine(
            "PRIVMSG #" + std::string(channel)
            + " :Failed to fetch Elo history"
          );
          co_return;
        }

        std::sort(
          historyArr.begin(),
          historyArr.end(),
          [](auto const& a, auto const& b){
            return a.as_object().at("date").to_number<int64_t>()
                 < b.as_object().at("date").to_number<int64_t>();
          }
        );

        int eloChange = 0;
        if (historyArr.size() >= 2) {
          auto& first = historyArr.front().as_object();
          auto& last  = historyArr.back().as_object();
          int firstElo = std::stoi(std::string(first.at("elo").as_string()));
          int lastElo  = std::stoi(std::string(last.at("elo").as_string()));
          eloChange = lastElo - firstElo;
        }

        // 9) Current Elo
        int currentElo = playerObj
          .at("games").as_object()
          .at("cs2").as_object()
          .at("faceit_elo").to_number<int>();

        // 10) Send response
        std::ostringstream oss;
        oss << "PRIVMSG #" << channel << " :"
            << wins << "W/" << losses << "L ("
            << statsArr.size() << ") | Elo " << currentElo
            << (eloChange >= 0 ? " (+" : " (") << eloChange << ")";
        co_await ircClient_->sendLine(oss.str());
        co_return;
      });

    // (5) Finally, create the IRC client
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
    // (1) Launch the top-level coroutine:
    boost::asio::co_spawn(
      ioc_,
      [this]() -> boost::asio::awaitable<void> {
        co_await runBot();
      },
      boost::asio::detached
    );

    // (2) Run the I/O loop until stopped
    ioc_.run();
}

boost::asio::awaitable<void> TwitchBot::runBot() {
    // 1) Compute which channels to join: persisted + controlChannel_
    auto channels = channelStore_->allChannels();
    if (std::find(channels.begin(), channels.end(), controlChannel_) == channels.end()) {
        channels.push_back(controlChannel_);
    }

    // 2) Connect and JOIN all channels
    try {
      co_await ircClient_->connect(channels);
    }
    catch (const std::exception& e) {
      std::cerr << "[TwitchBot] connect error: " << e.what() << "\n";
      co_return;
    }

    // 3) Spawn two background coroutines: pingLoop() and readLoop()
    auto executor = co_await boost::asio::this_coro::executor;

    // (a) Ping loop: send PING every X minutes
    boost::asio::co_spawn(
      executor,
      [this]() -> boost::asio::awaitable<void> {
        co_await ircClient_->pingLoop();
      },
      boost::asio::detached
    );

    // (b) Read loop: parse and dispatch every raw IRC line
    boost::asio::co_spawn(
      executor,
      [this]() -> boost::asio::awaitable<void> {
        co_await ircClient_->readLoop(
          [this](std::string_view rawLine) {
            // Debug: print the raw IRC line
            std::cout << "[IRC] " << rawLine << "\n";
            // Parse it into IrcMessage
            auto msg = parseIrcLine(rawLine);
            // Dispatch in its own coroutine so readLoop is never blocked
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

    // 4) Idle forever (until ioc_.stop())
    boost::asio::steady_timer idle{ioc_};
    idle.expires_at(std::chrono::steady_clock::time_point::max());
    co_await idle.async_wait(boost::asio::use_awaitable);
}

} // namespace twitch_bot
