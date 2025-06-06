#include "twitch_bot.hpp"
#include "message_parser.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

namespace twitch_bot {

TwitchBot::TwitchBot(std::string oauthToken,
                     std::string clientId,
                     std::string clientSecret,
                     std::string controlChannel)
    : ioc_()
    , ssl_ctx_(boost::asio::ssl::context::tlsv12_client)
    , oauthToken_(std::move(oauthToken))
    , clientId_(std::move(clientId))
    , clientSecret_(std::move(clientSecret))
    , controlChannel_(std::move(controlChannel))
{
    // Configure SSL context
    ssl_ctx_.set_default_verify_paths();

    // Load persisted channels from disk
    channelStore_ = std::make_unique<ChannelStore>();
    channelStore_->load();

    // Create HelixClient with shared I/O and SSL contexts
    helixClient_ = std::make_unique<HelixClient>(
        ioc_, ssl_ctx_, clientId_, clientSecret_);

    // Create CommandDispatcher
    dispatcher_ = std::make_unique<CommandDispatcher>();

    // Register "!join <channel>" (only allowed in controlChannel_)
    dispatcher_->registerCommand("!join",
        [this](std::string_view channel,
               std::string_view user,
               std::string_view args) -> boost::asio::awaitable<void>
        {
            // Only allow joins from the control channel
            if (std::string(channel) == controlChannel_) {
                // If args is non-empty, that is the channel to join;
                // otherwise use invoking user's name as the channel.
                std::string newChannel = !args.empty()
                    ? std::string(args)
                    : std::string(user);

                channelStore_->addChannel(newChannel);
                channelStore_->save();

                co_await ircClient_->sendLine("JOIN #" + newChannel);
                co_await ircClient_->sendLine(
                    "PRIVMSG #" + controlChannel_ + " :Joined " + newChannel);
            }
            co_return;
        });

    // Register "!leave <channel>" (only allowed in controlChannel_)
    dispatcher_->registerCommand("!leave",
        [this](std::string_view channel,
               std::string_view user,
               std::string_view args) -> boost::asio::awaitable<void>
        {
            // Only allow parts from the control channel
            if (std::string(channel) == controlChannel_) {
                // If args is non-empty, that is the channel to part;
                // otherwise use invoking user's name as the channel.
                std::string remChannel = !args.empty()
                    ? std::string(args)
                    : std::string(user);

                channelStore_->removeChannel(remChannel);
                channelStore_->save();

                co_await ircClient_->sendLine("PART #" + remChannel);
                co_await ircClient_->sendLine(
                    "PRIVMSG #" + controlChannel_ + " :Left " + remChannel);
            }
            co_return;
        });

    // Create IrcClient using shared I/O and SSL contexts
    ircClient_ = std::make_unique<IrcClient>(
        ioc_, ssl_ctx_, oauthToken_, controlChannel_);
}

TwitchBot::~TwitchBot() noexcept
{
    // Close the IRC connection (if it exists) and stop the I/O loop
    if (ircClient_) {
        ircClient_->close();
    }
    ioc_.stop();
}

void TwitchBot::addChatListener(ChatListener cb)
{
    dispatcher_->registerChatListener(std::move(cb));
}

void TwitchBot::run()
{
    // (1) Launch runBot() as a detached coroutine
    boost::asio::co_spawn(
        ioc_,
        [this]() -> boost::asio::awaitable<void> {
            co_await runBot();
        },
        boost::asio::detached);

    // (2) Run the I/O loop until shutdown
    ioc_.run();
}

boost::asio::awaitable<void> TwitchBot::runBot()
{
    // 1) Compute the set of channels to join (persisted + control channel)
    auto channels = channelStore_->allChannels();
    // Ensure we always join the control channel so we can receive commands
    if (std::find(channels.begin(), channels.end(), controlChannel_) ==
        channels.end())
    {
        channels.push_back(controlChannel_);
    }

    // 2) Establish the IRC connection and join all channels
    try {
        co_await ircClient_->connect(channels);
    }
    catch (const std::exception& e) {
        std::cerr << "[TwitchBot] connect error: " << e.what() << "\n";
        co_return;
    }

    // 3) Spawn two background coroutines: pingLoop() and readLoop()
    auto executor = co_await boost::asio::this_coro::executor;

    // (a) Ping loop: send PING every 4 minutes
    boost::asio::co_spawn(executor,
        [this]() -> boost::asio::awaitable<void> {
            co_await ircClient_->pingLoop();
        },
        boost::asio::detached);

    // (b) Read loop: for each raw IRC line, parse and dispatch
    boost::asio::co_spawn(executor,
        [this]() -> boost::asio::awaitable<void> {
            co_await ircClient_->readLoop(
                [this](std::string_view rawLine) {
                    // Debug print each raw IRC line
                    std::cout << "[IRC] " << rawLine << "\n";
                    // Parse into IrcMessage
                    auto msg = parseIrcLine(rawLine);
                    // Dispatch in its own coroutine so readLoop is not blocked
                    boost::asio::co_spawn(
                        ioc_,
                        dispatcher_->dispatch(msg),
                        boost::asio::detached);
                });
        },
        boost::asio::detached);

    // 4) Idle forever (until ioc_.stop() is called)
    boost::asio::steady_timer idle{ioc_};
    idle.expires_at(std::chrono::steady_clock::time_point::max());
    co_await idle.async_wait(boost::asio::use_awaitable);
}

} // namespace twitch_bot
