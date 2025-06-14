#include "twitch_bot.hpp"
#include "message_parser.hpp"

#include <array>
#include <iostream>
#include <span>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/steady_timer.hpp>

namespace twitch_bot {

TwitchBot::TwitchBot(std::string     oauth_token,
                     std::string     client_id,
                     std::string     client_secret,
                     std::string     control_channel,
                     std::size_t     threads)
  : ioc_{static_cast<int>(threads > 0 ? threads : 1)}
  , strand_{ioc_.get_executor()}
  , ssl_ctx_{boost::asio::ssl::context::tlsv12_client}
  , oauth_token_{std::move(oauth_token)}
  , client_id_{std::move(client_id)}
  , client_secret_{std::move(client_secret)}
  , control_channel_{std::move(control_channel)}
  , irc_client_{strand_, ssl_ctx_, oauth_token_, control_channel_}
  , dispatcher_{strand_}
  , helix_client_{strand_, ssl_ctx_, client_id_, client_secret_}
  , channel_store_{strand_}
  , thread_count_{threads > 0 ? threads : 1}
{
    ssl_ctx_.set_default_verify_paths();
    channel_store_.load();

    // !join
    dispatcher_.register_command(
        "join",
        [this](IrcMessage msg) noexcept -> boost::asio::awaitable<void> {
            auto channel = msg.params[0];
            auto user = msg.prefix;
            auto args = msg.trailing;
            bool is_mod = msg.is_moderator;
            bool is_broadcaster = msg.is_broadcaster;

            // only in control channel
            if (channel != control_channel_) co_return;

            if (!args.empty() && !is_broadcaster && !is_mod) co_return;

            std::string_view target = args.empty() ? user : args;

            if (channel_store_.contains(target))
            {
                std::array<boost::asio::const_buffer, 5> msg{{
                    boost::asio::buffer("PRIVMSG #", 9),
                    boost::asio::buffer(control_channel_),
                    boost::asio::buffer(" :Already in channel ", 22),
                    boost::asio::buffer(target),
                    boost::asio::buffer(CRLF)
                }};
                co_await irc_client_.send_buffers(msg);
                co_return;
            }

            channel_store_.add_channel(target);
            channel_store_.save();

            std::array<boost::asio::const_buffer, 3> join_cmd{{
                boost::asio::buffer("JOIN #", 6),
                boost::asio::buffer(target),
                boost::asio::buffer(CRLF)
            }};
            co_await irc_client_.send_buffers(join_cmd);

            std::array<boost::asio::const_buffer, 7> ack{{
                boost::asio::buffer("PRIVMSG #", 9),
                boost::asio::buffer(control_channel_),
                boost::asio::buffer(" :@", 3),
                boost::asio::buffer(user),
                boost::asio::buffer(" Joined ", 8),
                boost::asio::buffer(target),
                boost::asio::buffer(CRLF)
            }};
            co_await irc_client_.send_buffers(ack);
        });

    // !leave
    dispatcher_.register_command(
        "leave",
        [this](IrcMessage msg) noexcept -> boost::asio::awaitable<void> {
            auto channel = msg.params[0];
            auto user = msg.prefix;
            auto args = msg.trailing;
            bool is_mod = msg.is_moderator;
            bool is_broadcaster = msg.is_broadcaster;

            if (channel != control_channel_) co_return;

            if (!args.empty() && !is_broadcaster && !is_mod) co_return;

            std::string_view target = args.empty() ? user : args;

            if (!channel_store_.contains(target))
            {
                std::array<boost::asio::const_buffer, 5> msg{{
                    boost::asio::buffer("PRIVMSG #", 9),
                    boost::asio::buffer(control_channel_),
                    boost::asio::buffer(" :Not in channel ", 19),
                    boost::asio::buffer(target),
                    boost::asio::buffer(CRLF)
                }};
                co_await irc_client_.send_buffers(msg);
                co_return;
            }

            channel_store_.remove_channel(target);
            channel_store_.save();

            std::array<boost::asio::const_buffer, 3> part_cmd{{
                boost::asio::buffer("PART #", 6),
                boost::asio::buffer(target),
                boost::asio::buffer(CRLF)
            }};
            co_await irc_client_.send_buffers(part_cmd);

            std::array<boost::asio::const_buffer, 7> ack{{
                boost::asio::buffer("PRIVMSG #", 9),
                boost::asio::buffer(control_channel_),
                boost::asio::buffer(" :@", 3),
                boost::asio::buffer(user),
                boost::asio::buffer(" Left ", 6),
                boost::asio::buffer(target),
                boost::asio::buffer(CRLF)
            }};
            co_await irc_client_.send_buffers(ack);
        });
}

TwitchBot::~TwitchBot() noexcept
{
    irc_client_.close();
    ioc_.stop();
}

void TwitchBot::add_chat_listener(chat_listener_t listener)
{
    dispatcher_.register_chat_listener(std::move(listener));
}

void TwitchBot::run()
{
    boost::asio::co_spawn(
        strand_,
        run_bot(),
        boost::asio::detached);

    threads_.reserve(thread_count_ - 1);
    for (std::size_t i = 1; i < thread_count_; ++i)
    {
        threads_.emplace_back([this] { ioc_.run(); });
    }

    ioc_.run();

    for (auto& t : threads_) if (t.joinable()) t.join();
}

boost::asio::awaitable<void> TwitchBot::run_bot() noexcept
{
    // build channel list once
    std::vector<std::string_view> channels;
    channel_store_.channel_names(channels);
    if (std::find(channels.begin(), channels.end(),
                  control_channel_) == channels.end())
    {
        channels.push_back(control_channel_);
    }

    try
    {
        co_await irc_client_.connect(channels);
    }
    catch (const std::exception& e)
    {
        std::cerr << "[TwitchBot] connect error: "
                  << e.what() << "\n";
        co_return;
    }

    auto exec = co_await boost::asio::this_coro::executor;

    // ping loop
    boost::asio::co_spawn(
        exec,
        [this]() noexcept -> boost::asio::awaitable<void> {
            co_await irc_client_.ping_loop();
        },
        boost::asio::detached);

    // read + dispatch loop
    boost::asio::co_spawn(
        exec,
        [this]() noexcept -> boost::asio::awaitable<void> {
            co_await irc_client_.read_loop(
                [this](std::string_view raw) {
                    std::cout << "[IRC] " << raw << '\n';
                    auto msg = parse_irc_line(raw);
                    dispatcher_.dispatch(msg);
                });
        },
        boost::asio::detached);

    // idle forever
    boost::asio::steady_timer idle{ioc_};
    idle.expires_at(std::chrono::steady_clock::time_point::max());
    co_await idle.async_wait(boost::asio::use_awaitable);
}

} // namespace twitch_bot
