#include "twitch_bot.hpp"
#include "message_parser.hpp"

#include <algorithm>
#include <iostream>
#include <vector>
#include <string_view>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/websocket/ssl.hpp>

namespace twitch_bot {

TwitchBot::TwitchBot(std::string oauthToken,
                     std::string clientId,
                     std::string clientSecret,
                     std::string controlChannel,
                     std::size_t threads)
  : ioc_(static_cast<int>(threads > 0 ? threads : 1))
  , ssl_ctx_(boost::asio::ssl::context::tlsv12_client)
  , controlChannel_(std::move(controlChannel))
  , ircClient_(ioc_.get_executor(),
               ssl_ctx_,
               oauthToken,
               controlChannel_)
    , dispatcher_(
            ioc_.get_executor()
      )
  , helixClient_(ioc_.get_executor(),
                 ssl_ctx_,
                 clientId,
                 clientSecret)
  , channelStore_(ioc_.get_executor())
  , threadCount_(threads > 0 ? threads : 1)
  , oauthToken_(std::move(oauthToken))
  , clientId_(std::move(clientId))
  , clientSecret_(std::move(clientSecret))
{
    ssl_ctx_.set_default_verify_paths();
    channelStore_.load();

    // !join handler
    dispatcher_.registerCommand(
        "join",
        [this](std::string_view channel,
            std::string_view user,
            std::string_view args,
            bool isModerator) -> boost::asio::awaitable<void>
        {
            // 1) Only react in our control channel
            if (channel != controlChannel_)
                co_return;

            // 2) Only the broadcaster or a moderator may join arbitrary channels
            bool isBroadcaster = (user == channel);
            if (!args.empty() && !isBroadcaster && !isModerator)
                co_return;

            // 3) Figure out which channel to join
            std::string_view target = args.empty() ? user : args;

            // 4) Already in store?
            if (channelStore_.contains(target)) {
                std::string msg;
                msg.reserve(11 + controlChannel_.size() + 21 + target.size());
                msg.append("PRIVMSG #")
                    .append(controlChannel_)
                    .append(" :Already in channel ")
                    .append(target);
                co_await ircClient_.sendLine(msg);
                co_return;
            }

            // 5) Persist new channel
            channelStore_.addChannel(target);
            channelStore_.save();

            // 6) JOIN with zero heap allocations
            std::array<boost::asio::const_buffer, 3> joinCmd{ {
                boost::asio::buffer("JOIN #", 6),
                boost::asio::buffer(target),
                boost::asio::buffer("\r\n", 2)
            } };
            co_await ircClient_.sendBuffers(joinCmd);

            // 7) Acknowledge the join in control channel
            std::string ack;
            ack.reserve(12 + controlChannel_.size()
                + 3 + user.size()
                + 8 + target.size());
            ack.append("PRIVMSG #")
                .append(controlChannel_)
                .append(" :@")
                .append(user)
                .append(" Joined ")
                .append(target);
            co_await ircClient_.sendLine(ack);
        });

    // !leave handler
    dispatcher_.registerCommand(
        "leave",
        [this](std::string_view channel,
            std::string_view user,
            std::string_view args,
            bool isModerator) -> boost::asio::awaitable<void>
        {
            // 1) Only react in our control channel
            if (channel != controlChannel_)
                co_return;

            // 2) Only the broadcaster or a moderator may remove arbitrary channels
            bool isBroadcaster = (user == channel);
            if (!args.empty() && !isBroadcaster && !isModerator)
                co_return;

            // 3) Determine the target channel
            std::string_view target = args.empty() ? user : args;

            // 4) If not in store, notify and return
            if (!channelStore_.contains(target)) {
                std::string msg;
                msg.reserve(11 + controlChannel_.size() + 17 + target.size());
                msg.append("PRIVMSG #")
                    .append(controlChannel_)
                    .append(" :Not in channel ")
                    .append(target);
                co_await ircClient_.sendLine(msg);
                co_return;
            }

            // 5) Remove from store and persist
            channelStore_.removeChannel(target);
            channelStore_.save();

            // 6) PART with zero heap allocations
            std::array<boost::asio::const_buffer, 3> partCmd{ {
                boost::asio::buffer("PART #", 6),
                boost::asio::buffer(target),
                boost::asio::buffer("\r\n", 2)
            } };
            co_await ircClient_.sendBuffers(partCmd);

            // 7) Acknowledge the leave
            std::string ack;
            ack.reserve(12 + controlChannel_.size()
                + 3 + user.size()
                + 6 + target.size());
            ack.append("PRIVMSG #")
                .append(controlChannel_)
                .append(" :@")
                .append(user)
                .append(" Left ")
                .append(target);
            co_await ircClient_.sendLine(ack);
        });
}

TwitchBot::~TwitchBot() noexcept
{
    ircClient_.close();
    ioc_.stop();
}

void TwitchBot::addChatListener(ChatListener cb)
{
    dispatcher_.registerChatListener(std::move(cb));
}

void TwitchBot::run()
{
    // Spawn main coroutine
    boost::asio::co_spawn(ioc_, runBot(), boost::asio::detached);

    // Launch additional I/O threads
    threads_.reserve(threadCount_ - 1);
    for (std::size_t i = 1; i < threadCount_; ++i) {
        threads_.emplace_back([this]{ ioc_.run(); });
    }

    // Main thread
    ioc_.run();

    // Join helpers
    for (auto& t : threads_) if (t.joinable()) t.join();
}

boost::asio::awaitable<void> TwitchBot::runBot()
{
    // Reuse store’s string_view list directly
    auto channels = channelStore_.channelNames();
    if (std::find(channels.begin(), channels.end(),
                  controlChannel_) == channels.end())
    {
        channels.push_back(controlChannel_);
    }

    try {
        co_await ircClient_.connect(channels);
    }
    catch (const std::exception& e) {
        std::cerr << "[TwitchBot] connect error: " << e.what() << "\n";
        co_return;
    }

    // Ping loop
    auto exec = co_await boost::asio::this_coro::executor;
    boost::asio::co_spawn(exec,
        [this]() -> boost::asio::awaitable<void> {
            co_await ircClient_.pingLoop();
        },
        boost::asio::detached);

    // Read loop — format and inline dispatch
    boost::asio::co_spawn(
        exec,
        [this]() -> boost::asio::awaitable<void>
        {
            co_await ircClient_.readLoop(
                [this](std::string_view raw_line)
                {
                    std::cout << "[IRC] " << raw_line << '\n';
                    twitch_bot::IrcMessage msg;
                    // call the header-only parser
                    parseIrcLine(raw_line.data(),
                        raw_line.size(),
                        msg);

                    dispatcher_.dispatch(msg);
                });
        },
        boost::asio::detached);

    // Idle forever
    boost::asio::steady_timer idle{ioc_};
    idle.expires_at(std::chrono::steady_clock::time_point::max());
    co_await idle.async_wait(boost::asio::use_awaitable);
}

} // namespace twitch_bot
