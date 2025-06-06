#include "twitch_bot.hpp"
#include "message_parser.hpp"

#include <iostream>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/steady_timer.hpp>

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
    ssl_ctx_.set_default_verify_paths();

    // Load persisted channels from disk
    channelStore_ = std::make_unique<ChannelStore>();
    channelStore_->load();

    // Create HelixClient with same I/O and SSL contexts
    helixClient_ = std::make_unique<HelixClient>(
        ioc_, ssl_ctx_, clientId_, clientSecret_);

    // Create CommandDispatcher
    dispatcher_ = std::make_unique<CommandDispatcher>();

    // "!join <channel>" (only allowed in controlChannel_)
    dispatcher_->registerCommand("!join",
        [this](std::string_view channel,
               std::string_view user,
               std::string_view args) -> boost::asio::awaitable<void>
        {
            if (std::string(channel) == controlChannel_) {
                // If args is non-empty, use that as the channel to join;
                // otherwise fall back to the invoking user's own channel.
                std::string newChan = !args.empty()
                    ? std::string(args)
                    : std::string(user);

                channelStore_->addChannel(newChan);
                channelStore_->save();

                co_await ircClient_->sendLine("JOIN #" + newChan);
                co_await ircClient_->sendLine(
                    "PRIVMSG #" + controlChannel_ + " :Joined " + newChan
                );
            }
            co_return;
        });

    // "!leave <channel>" (only allowed in controlChannel_)
    dispatcher_->registerCommand("!leave",
        [this](std::string_view channel,
               std::string_view user,
               std::string_view args) -> boost::asio::awaitable<void>
        {
            if (std::string(channel) == controlChannel_) {
                // If args is non-empty, use that as the channel to part;
                // otherwise fall back to the invoking user's own channel.
                std::string remChan = !args.empty()
                    ? std::string(args)
                    : std::string(user);

                channelStore_->removeChannel(remChan);
                channelStore_->save();

                co_await ircClient_->sendLine("PART #" + remChan);
                co_await ircClient_->sendLine(
                    "PRIVMSG #" + controlChannel_ + " :Left " + remChan
                );
            }
            co_return;
        });

    // Create IrcClient using same I/O and SSL contexts
    ircClient_ = std::make_unique<IrcClient>(
        ioc_, ssl_ctx_, oauthToken_, controlChannel_);
}

TwitchBot::~TwitchBot() noexcept
{
    // Ensure the IRC connection closes and I/O is stopped
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
    // (1) Launch runBot() as a coroutine
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
    // Always join the control channel so we can receive commands
    if (std::find(channels.begin(), channels.end(), controlChannel_) == channels.end()) {
        channels.push_back(controlChannel_);
    }

    // 2) Establish the IRC connection and join channels
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
                    // Debug print each raw IRC line:
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

    // 4) Idle forever (until ioc_.stop())
    boost::asio::steady_timer idle{ioc_};
    idle.expires_at(std::chrono::steady_clock::time_point::max());
    co_await idle.async_wait(boost::asio::use_awaitable);
}

} // namespace twitch_bot