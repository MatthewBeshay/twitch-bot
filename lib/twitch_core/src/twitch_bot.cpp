// C++ Standard Library
#include <array>
#include <format>
#include <iostream>
#include <random>
#include <span>

// 3rd-party
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>

// Project
#include <tb/parser/irc_message_parser.hpp>
#include <tb/twitch/twitch_bot.hpp>

namespace twitch_bot {

TwitchBot::TwitchBot(std::string access_token,
                     std::string refresh_token,
                     std::string client_id,
                     std::string client_secret,
                     std::string control_channel,
                     std::size_t threads)
    : pool_{threads > 0 ? threads : 1} // I/O thread-pool
    , strand_{pool_.get_executor()} // serializes all work
    , ssl_ctx_{boost::asio::ssl::context::tlsv12_client}
    , access_token_{std::move(access_token)}
    , refresh_token_(std::move(refresh_token))
    , client_id_{std::move(client_id)}
    , client_secret_{std::move(client_secret)}
    , control_channel_{std::move(control_channel)}
    , irc_client_{strand_, ssl_ctx_, access_token_, control_channel_}
    , dispatcher_{strand_}
    , helix_client_{strand_, ssl_ctx_, client_id_, client_secret_, refresh_token_}
    , channel_store_{strand_}
{
    ssl_ctx_.set_default_verify_paths();
    channel_store_.load();

    // ---------- !join ---------------------------------------------------------
    dispatcher_.register_command(
        "join", [this](IrcMessage msg) noexcept -> boost::asio::awaitable<void> {
            auto channel = msg.params[0];
            auto user = msg.prefix;
            auto args = msg.trailing;

            // parent id for threaded reply
            const std::string_view parent_id = msg.get_tag("id");

            if (channel != control_channel_) // only from control channel
                co_return;

            // mods may target another channel
            if (!args.empty() && !is_privileged(msg)) {
                std::string warn;
                warn.reserve(128 + user.size());
                warn.append("@")
                    .append(user)
                    .append(" You must be a mod to invite the bot to a different channel. ")
                    .append("Use !join from your own channel instead.");
                co_await irc_client_.reply(control_channel_, parent_id, warn);
                co_return;
            }

            std::string_view target = args.empty() ? user : args;

            // ignore duplicates
            if (channel_store_.contains(target)) {
                std::string s;
                s.reserve(22 + target.size());
                s.append("Already in channel ").append(target);
                co_await irc_client_.reply(control_channel_, parent_id, s);
                co_return;
            }

            channel_store_.add_channel(target);
            channel_store_.save();

            // JOIN #<target>
            {
                std::array<boost::asio::const_buffer, 3> join_cmd{boost::asio::buffer("JOIN #", 6),
                                                                  boost::asio::buffer(target),
                                                                  boost::asio::buffer(kCRLF)};
                co_await irc_client_.send_buffers(join_cmd);
            }

            // acknowledgement as a threaded reply
            {
                std::string ack;
                ack.reserve(8 + target.size());
                ack.append("Joined ").append(target);
                co_await irc_client_.reply(control_channel_, parent_id, ack);
            }
        });

    // ---------- !leave --------------------------------------------------------
    dispatcher_.register_command(
        "leave", [this](IrcMessage msg) noexcept -> boost::asio::awaitable<void> {
            auto channel = msg.params[0];
            auto user = msg.prefix;
            auto args = msg.trailing;

            // parent id for threaded reply
            const std::string_view parent_id = msg.get_tag("id");

            if (channel != control_channel_)
                co_return;

            // mods may target another channel
            if (!args.empty() && !is_privileged(msg)) {
                std::string warn;
                warn.reserve(120 + user.size());
                warn.append("@")
                    .append(user)
                    .append(" You must be a mod to remove the bot from another channel. ")
                    .append("Use !leave from your own channel instead.");
                co_await irc_client_.reply(control_channel_, parent_id, warn);
                co_return;
            }

            std::string_view target = args.empty() ? user : args;

            if (!channel_store_.contains(target)) {
                std::string s;
                s.reserve(19 + target.size());
                s.append("Not in channel ").append(target);
                co_await irc_client_.reply(control_channel_, parent_id, s);
                co_return;
            }

            channel_store_.remove_channel(target);
            channel_store_.save();

            // PART #<target>
            {
                std::array<boost::asio::const_buffer, 3> part_cmd{boost::asio::buffer("PART #", 6),
                                                                  boost::asio::buffer(target),
                                                                  boost::asio::buffer(kCRLF)};
                co_await irc_client_.send_buffers(part_cmd);
            }

            // acknowledgement as a threaded reply
            {
                std::string ack;
                ack.reserve(6 + target.size());
                ack.append("Left ").append(target);
                co_await irc_client_.reply(control_channel_, parent_id, ack);
            }
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

            std::string text = std::format("Currently in channels: {}", list);
            co_await irc_client_.privmsg_wrap(control_channel_, text);
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

// --------------------- public chat helpers -----------------------------------

boost::asio::awaitable<void> TwitchBot::say(std::string_view channel, std::string_view text)
{
    // Ensure serialization on our strand before touching IRC client
    co_await boost::asio::dispatch(strand_, boost::asio::use_awaitable);
    co_await irc_client_.privmsg_wrap(channel, text);
}

boost::asio::awaitable<void>
TwitchBot::reply(std::string_view channel, std::string_view parent_msg_id, std::string_view text)
{
    co_await boost::asio::dispatch(strand_, boost::asio::use_awaitable);
    if (parent_msg_id.empty()) {
        co_await irc_client_.privmsg_wrap(channel, text);
    } else {
        co_await irc_client_.reply(channel, parent_msg_id, text);
    }
}

boost::asio::awaitable<void> TwitchBot::run_bot()
{
    using namespace std::chrono;

    // Exponential backoff with full jitter.
    static constexpr auto kConnectBase = seconds{3};
    static constexpr auto kReconnectBase = seconds{2};
    static constexpr auto kBackoffCap = seconds{30};
    static constexpr auto kMinSleep = milliseconds{150}; // avoid 0ms spins

    auto next_backoff
        = [](unsigned& attempts, milliseconds base, milliseconds cap) -> milliseconds {
        // Growth: base * 2^attempts, capped
        const unsigned exp = std::min<unsigned>(attempts, 16); // guard overflow
        const auto grown = base * (1u << exp);
        const auto max_d = grown > cap ? cap : grown;

        // Full jitter: uniform [0, max_d]
        static thread_local std::mt19937 rng{std::random_device{}()};
        const auto ms_max = duration_cast<milliseconds>(max_d).count();
        std::uniform_int_distribution<long long> dist(0, ms_max);

        ++attempts;
        auto d = milliseconds{dist(rng)};
        if (d < kMinSleep)
            d = kMinSleep; // tiny floor
        return d;
    };

    unsigned connect_attempts = 0; // while initial connects fail
    unsigned reconnect_attempts = 0; // while reconnect cycles fail

    for (;;) {
        // Build the channel list fresh so it reflects !join/!leave while running.
        std::vector<std::string_view> channels;
        channel_store_.channel_names(channels);
        if (std::find(channels.begin(), channels.end(), control_channel_) == channels.end()) {
            channels.push_back(control_channel_);
        }

        // Ensure we have a valid token and set it on the IRC client.
        co_await helix_client_.ensure_valid_token();

        std::string access_token = helix_client_.current_token();
        if (access_token.rfind("oauth:", 0) != 0) {
            access_token = "oauth:" + access_token;
        }
        irc_client_.set_access_token(access_token);

        // Connect (avoid co_await in catch on MSVC).
        bool connected = false;
        try {
            co_await irc_client_.connect(channels);
            connected = true;
        } catch (const std::exception& e) {
            std::cerr << "[TwitchBot] IRC connect error: " << e.what() << '\n';
        }
        if (!connected) {
            const auto delay = next_backoff(connect_attempts,
                                            duration_cast<milliseconds>(kConnectBase),
                                            duration_cast<milliseconds>(kBackoffCap));
            std::cout << "[TwitchBot] backoff#" << connect_attempts
                      << " reason=connect-error sleep=" << delay.count() << "ms\n";
            boost::asio::steady_timer pause{pool_};
            pause.expires_after(delay);
            co_await pause.async_wait(boost::asio::use_awaitable);
            continue; // try again
        }

        // Successful connect — reset backoff counters.
        connect_attempts = 0;
        reconnect_attempts = 0;

        auto exec = co_await boost::asio::this_coro::executor;

        // A timer used as a simple wake-up signal to trigger reconnect.
        boost::asio::steady_timer reconnect_signal{pool_};
        reconnect_signal.expires_at(std::chrono::steady_clock::time_point::max());

        // reason for reconnect (for telemetry)
        std::string reconnect_reason;

        // Client-initiated keepalive PING every four minutes.
        boost::asio::co_spawn(
            exec,
            [this]() noexcept -> boost::asio::awaitable<void> { co_await irc_client_.ping_loop(); },
            boost::asio::detached);

        // Read and dispatch loop. On RECONNECT or read error, wake the supervisor to reconnect.
        boost::asio::co_spawn(
            exec,
            [this, &reconnect_signal, exec, &reconnect_reason]() noexcept
                -> boost::asio::awaitable<void> {
                try {
                    co_await irc_client_.read_loop([this,
                                                    &reconnect_signal,
                                                    exec,
                                                    &reconnect_reason](std::string_view raw) {
                        std::cout << "[IRC] " << raw << '\n';
                        auto msg = parse_irc_line(raw);

                        // Mandatory PING -> PONG.
                        if (msg.command == "PING") {
                            auto payload = std::string{msg.trailing};
                            boost::asio::co_spawn(
                                exec,
                                [this,
                                 payload = std::move(payload)]() -> boost::asio::awaitable<void> {
                                    std::array<boost::asio::const_buffer, 4> bufs{
                                        boost::asio::buffer("PONG ", 5),
                                        boost::asio::buffer(":", 1), boost::asio::buffer(payload),
                                        boost::asio::buffer("\r\n", 2)};
                                    co_await irc_client_.send_buffers(bufs);
                                    co_return;
                                },
                                boost::asio::detached);
                            return;
                        }

                        // Server-initiated maintenance reconnect.
                        if (msg.command == "RECONNECT") {
                            reconnect_reason = "server-reconnect";
                            irc_client_.close();
                            reconnect_signal.cancel();
                            return;
                        }

                        // Treat auth-related NOTICEs as fatal and refresh.
                        if (msg.command == "NOTICE") {
                            auto id = msg.get_tag("msg-id"); // prefer msg-id when present
                            if (id == "msg_auth_failed"
                                || msg.trailing == "Login authentication failed"
                                || msg.trailing == "Improperly formatted auth") {
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

                        // Capability negotiation results
                        if (msg.command == "CAP" && msg.parameters().size() >= 2) {
                            auto sub = msg.parameters()[1]; // "ACK" or "NAK"
                            if (sub == "ACK") {
                                std::cout << "[IRC] CAP ACK " << msg.trailing << '\n';
                            } else if (sub == "NAK") {
                                std::cerr << "[IRC] CAP NAK " << msg.trailing
                                          << " - tags/commands/membership may not be available\n";
                            }
                            return; // no further routing
                        }

                        // ROOMSTATE -> re-apply channel modes/rate-limits if you maintain them
                        // if (msg.command == "ROOMSTATE") {
                        // chat_rate_limiter_.apply_roomstate(msg); }

                        // Normal chat routing.
                        dispatcher_.dispatch(std::move(msg));
                    });
                } catch (...) {
                    if (reconnect_reason.empty())
                        reconnect_reason = "read-error";
                    reconnect_signal.cancel();
                }
                co_return;
            },
            boost::asio::detached);

        // Wait until either RECONNECT was seen or the read loop exited.
        try {
            co_await reconnect_signal.async_wait(boost::asio::use_awaitable);
        } catch (...) {
            // Timer is cancelled to wake us - ignore cancellation.
        }

        // Close out the current connection before looping back to reconnect.
        irc_client_.close();

        // Exponential backoff with jitter before reconnect attempt.
        const auto delay = next_backoff(reconnect_attempts,
                                        duration_cast<milliseconds>(kReconnectBase),
                                        duration_cast<milliseconds>(kBackoffCap));
        std::cout << "[TwitchBot] backoff#" << reconnect_attempts
                  << " reason=" << (reconnect_reason.empty() ? "unknown" : reconnect_reason)
                  << " sleep=" << delay.count() << "ms\n";

        boost::asio::steady_timer pause{pool_};
        pause.expires_after(delay);
        co_await pause.async_wait(boost::asio::use_awaitable);
        // loop to reconnect
    }
}

} // namespace twitch_bot
