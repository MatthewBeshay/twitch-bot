// Twitch bot core supervisor.
// Coordinates IRC, Helix, command dispatch, and reconnect/backoff strategy.
// Rationale:
// - Serialise all state changes and socket writes via a strand to avoid races.
// - Keep reconnects bounded and jittered to prevent thundering herd.
// - Keep control-channel always joined; persist user-joined channels across reconnects.

// C++ Standard Library
#include <algorithm>
#include <array>
#include <iostream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

// Boost.Asio
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>

// Core
#include <tb/parser/irc_message_parser.hpp>
#include <tb/twitch/twitch_bot.hpp>

namespace twitch_bot
{

    TwitchBot::TwitchBot(std::string access_token,
                         std::string refresh_token,
                         std::string client_id,
                         std::string client_secret,
                         std::string control_channel,
                         std::size_t threads)
        // One pool for I/O; keep it small and fixed.
        :
        pool_{ threads > 0 ? threads : 1 }
        // Serialise all bot state transitions (handlers, sends, reconnect).
        ,
        strand_{ pool_.get_executor() },
        ssl_ctx_{ boost::asio::ssl::context::tlsv12_client },
        access_token_{ std::move(access_token) },
        refresh_token_(std::move(refresh_token)),
        client_id_{ std::move(client_id) },
        client_secret_{ std::move(client_secret) },
        control_channel_{ std::move(control_channel) },
        irc_client_{ strand_, ssl_ctx_, access_token_, control_channel_ },
        dispatcher_{ strand_ },
        helix_client_{ strand_, ssl_ctx_, client_id_, client_secret_, refresh_token_ }
    {
        // Use platform store (keeps cert management out of the bot).
        ssl_ctx_.set_default_verify_paths();
    }

    TwitchBot::~TwitchBot() noexcept
    {
        // Best-effort: stop timers and close the socket.
        irc_client_.close();
    }

    void TwitchBot::add_chat_listener(chat_listener_t listener)
    {
        dispatcher_.register_chat_listener(std::move(listener));
    }

    void TwitchBot::run()
    {
        // Run the supervisor on our strand; block until the pool stops.
        boost::asio::co_spawn(strand_, run_bot(), boost::asio::detached);
        pool_.join();
    }

    void TwitchBot::set_initial_channels(std::vector<std::string> channels)
    {
        std::lock_guard lk(chan_mutex_);
        initial_channels_ = std::move(channels);
    }

    boost::asio::awaitable<void> TwitchBot::join_channel(std::string_view channel)
    {
        // All socket operations go through the strand (shared state + ordering).
        co_await boost::asio::dispatch(strand_, boost::asio::use_awaitable);

        // IRC: JOIN #<channel>.
        co_await irc_client_.join(channel);

        // Persist in-memory intent so reconnects re-join.
        {
            std::lock_guard lk(chan_mutex_);
            std::string c{ channel };
            if (std::find(initial_channels_.begin(), initial_channels_.end(), c) == initial_channels_.end())
            {
                initial_channels_.push_back(std::move(c));
            }
        }
    }

    boost::asio::awaitable<void> TwitchBot::part_channel(std::string_view channel)
    {
        co_await boost::asio::dispatch(strand_, boost::asio::use_awaitable);

        co_await irc_client_.part(channel);

        // Stop auto-rejoining on future reconnects.
        {
            std::lock_guard lk(chan_mutex_);
            const std::string c{ channel };
            auto it = std::find(initial_channels_.begin(), initial_channels_.end(), c);
            if (it != initial_channels_.end())
            {
                initial_channels_.erase(it);
            }
        }
    }

    boost::asio::awaitable<void> TwitchBot::say(std::string_view channel, std::string_view text)
    {
        // Keep ordering and avoid interleaving with other sends.
        co_await boost::asio::dispatch(strand_, boost::asio::use_awaitable);
        co_await irc_client_.privmsg_wrap(channel, text);
    }

    boost::asio::awaitable<void>
    TwitchBot::reply(std::string_view channel, std::string_view parent_msg_id, std::string_view text)
    {
        co_await boost::asio::dispatch(strand_, boost::asio::use_awaitable);
        if (parent_msg_id.empty())
        {
            co_await irc_client_.privmsg_wrap(channel, text);
        }
        else
        {
            co_await irc_client_.reply(channel, parent_msg_id, text);
        }
    }

    boost::asio::awaitable<void> TwitchBot::run_bot()
    {
        using namespace std::chrono;

        // Reconnect policy: exponential backoff with full jitter.
        static constexpr auto k_connect_base = seconds{ 3 };
        static constexpr auto k_reconnect_base = seconds{ 2 };
        static constexpr auto k_backoff_cap = seconds{ 30 };
        static constexpr auto k_min_sleep = milliseconds{ 150 };

        auto next_backoff = [](unsigned& attempts, milliseconds base, milliseconds cap) -> milliseconds {
            // Grows like base * 2^attempts, capped; randomise to avoid thundering herd.
            const unsigned exp = std::min<unsigned>(attempts, 16);
            const auto grown = base * (1u << exp);
            const auto max_d = grown > cap ? cap : grown;

            static thread_local std::mt19937 rng{ std::random_device{}() };
            const auto ms_max = duration_cast<milliseconds>(max_d).count();
            std::uniform_int_distribution<long long> dist(0, ms_max);

            ++attempts;
            auto d = milliseconds{ dist(rng) };
            if (d < k_min_sleep)
            {
                d = k_min_sleep;
            }
            return d;
        };

        unsigned connect_attempts = 0;
        unsigned reconnect_attempts = 0;

        for (;;)
        {
            // Snapshot channel list under lock; always include control channel.
            std::vector<std::string_view> channels;
            {
                std::lock_guard lk(chan_mutex_);
                channels.reserve(initial_channels_.size() + 1);
                for (auto& c : initial_channels_)
                {
                    channels.push_back(c);
                }
            }
            if (std::find(channels.begin(), channels.end(), control_channel_) == channels.end())
            {
                channels.push_back(control_channel_);
            }

            // Ensure fresh OAuth, then update IRC client token.
            co_await helix_client_.ensure_valid_token();
            std::string access_token = helix_client_.current_token();
            if (access_token.rfind("oauth:", 0) != 0)
            {
                access_token = "oauth:" + access_token;
            }
            irc_client_.set_access_token(access_token);

            bool connected = false;
            try
            {
                co_await irc_client_.connect(channels);
                connected = true;
            }
            catch (const std::exception& e)
            {
                std::cerr << "[TwitchBot] IRC connect error: " << e.what() << '\n';
            }
            if (!connected)
            {
                const auto delay = next_backoff(connect_attempts,
                                                duration_cast<milliseconds>(k_connect_base),
                                                duration_cast<milliseconds>(k_backoff_cap));
                std::cout << "[TwitchBot] backoff#" << connect_attempts
                          << " reason=connect-error sleep=" << delay.count() << "ms\n";
                boost::asio::steady_timer pause{ pool_ };
                pause.expires_after(delay);
                co_await pause.async_wait(boost::asio::use_awaitable);
                continue;
            }

            // Connected: reset counters.
            connect_attempts = 0;
            reconnect_attempts = 0;

            auto exec = co_await boost::asio::this_coro::executor;

            // Signal used to break out to the reconnect path.
            boost::asio::steady_timer reconnect_signal{ pool_ };
            reconnect_signal.expires_at(std::chrono::steady_clock::time_point::max());

            std::string reconnect_reason;

            // Keep the link alive.
            boost::asio::co_spawn(
                exec,
                [this]() noexcept -> boost::asio::awaitable<void> { co_await irc_client_.ping_loop(); },
                boost::asio::detached);

            // Read loop and routing.
            boost::asio::co_spawn(
                exec,
                [this, &reconnect_signal, exec, &reconnect_reason]() noexcept
                    -> boost::asio::awaitable<void> {
                    try
                    {
                        co_await irc_client_.read_loop(
                            [this, &reconnect_signal, exec, &reconnect_reason](std::string_view raw) {
                                std::cout << "[IRC] " << raw << '\n';
                                auto msg = parse_irc_line(raw);

                                if (msg.command == "PING")
                                {
                                    // Reply with PONG; keep payload as-is.
                                    auto payload = std::string{ msg.trailing };
                                    boost::asio::co_spawn(
                                        exec,
                                        [this, payload = std::move(payload)]()
                                            -> boost::asio::awaitable<void> {
                                            std::array<boost::asio::const_buffer, 4> bufs{
                                                boost::asio::buffer("PONG ", 5),
                                                boost::asio::buffer(":", 1),
                                                boost::asio::buffer(payload),
                                                boost::asio::buffer("\r\n", 2)
                                            };
                                            co_await irc_client_.send_buffers(bufs);
                                            co_return;
                                        },
                                        boost::asio::detached);
                                    return;
                                }

                                if (msg.command == "RECONNECT")
                                {
                                    reconnect_reason = "server-reconnect";
                                    irc_client_.close();
                                    reconnect_signal.cancel();
                                    return;
                                }

                                if (msg.command == "NOTICE")
                                {
                                    // Detect auth errors and trigger token refresh.
                                    auto id = msg.get_tag("msg-id");
                                    if (id == "msg_auth_failed" || msg.trailing == "Login authentication failed" || msg.trailing == "Improperly formatted auth")
                                    {
                                        reconnect_reason = "auth-fail";
                                        boost::asio::co_spawn(
                                            exec,
                                            [this]() -> boost::asio::awaitable<void> {
                                                co_await helix_client_.ensure_valid_token();
                                                co_return;
                                            },
                                            boost::asio::detached);
                                        irc_client_.close();
                                        reconnect_signal.cancel();
                                        return;
                                    }
                                }

                                if (msg.command == "CAP" && msg.parameters().size() >= 2)
                                {
                                    auto sub = msg.parameters()[1]; // "ACK" / "NAK"
                                    if (sub == "ACK")
                                    {
                                        std::cout << "[IRC] CAP ACK " << msg.trailing << '\n';
                                    }
                                    else if (sub == "NAK")
                                    {
                                        std::cerr << "[IRC] CAP NAK " << msg.trailing
                                                  << " (tags/commands/membership may be unavailable)\n";
                                    }
                                    return;
                                }

                                // Normal chat routing.
                                dispatcher_.dispatch(std::move(msg));
                            });
                    }
                    catch (...)
                    {
                        if (reconnect_reason.empty())
                        {
                            reconnect_reason = "read-error";
                        }
                        reconnect_signal.cancel();
                    }
                    co_return;
                },
                boost::asio::detached);

            try
            {
                co_await reconnect_signal.async_wait(boost::asio::use_awaitable);
            }
            catch (...)
            {
                // Woken by cancel(); continue to reconnect.
            }

            // Close the current connection before backing off and retrying.
            irc_client_.close();

            const auto delay = next_backoff(reconnect_attempts,
                                            duration_cast<milliseconds>(k_reconnect_base),
                                            duration_cast<milliseconds>(k_backoff_cap));
            std::cout << "[TwitchBot] backoff#" << reconnect_attempts
                      << " reason=" << (reconnect_reason.empty() ? "unknown" : reconnect_reason)
                      << " sleep=" << delay.count() << "ms\n";

            boost::asio::steady_timer pause{ pool_ };
            pause.expires_after(delay);
            co_await pause.async_wait(boost::asio::use_awaitable);
            // loop and reconnect
        }
    }

} // namespace twitch_bot
