#pragma once

// C++ Standard Library
#include <chrono>
#include <deque>
#include <string>
#include <string_view>
#include <unordered_map>

// 3rd-party
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/use_awaitable.hpp>

// Project
#include "irc_client.hpp"
#include <tb/utils/attributes.hpp>
#include <tb/utils/transparent_string_hash.hpp>

namespace twitch_bot {

// Minimal chat rate limiter for Twitch IRC.
// Rules enforced:
// - Global: 20 PRIVMSG per 30 seconds
// - Per channel: 1 PRIVMSG per second
class ChatRateLimiter
{
public:
    explicit ChatRateLimiter(boost::asio::any_io_executor exec) noexcept : strand_{exec}
    {
    }

    // Send "PRIVMSG #channel :text" once limits permit.
    // Channel must not include the leading '#'.
    boost::asio::awaitable<void>
    send_privmsg(IrcClient& irc, std::string_view channel, std::string_view text) noexcept;

    // Reset internal counters, eg after reconnect.
    void reset() noexcept
    {
        global_sends_.clear();
        next_per_channel_.clear();
    }

private:
    using clock = std::chrono::steady_clock;

    static constexpr int kGlobalBurst = 20;
    static constexpr auto kGlobalWindow = std::chrono::seconds{30};
    static constexpr auto kPerChannelGap = std::chrono::seconds{1};

    boost::asio::strand<boost::asio::any_io_executor> strand_;

    // Timestamps of recent sends within the global window
    std::deque<clock::time_point> global_sends_;

    // Next allowed send time per channel
    std::unordered_map<std::string,
                       clock::time_point,
                       TransparentBasicStringHash<char>,
                       TransparentBasicStringEq<char>>
        next_per_channel_;
};

} // namespace twitch_bot

/*
    Wire in chat rate limiting

    Global: 20 messages per 30 seconds.

    Per channel: 1 Hz baseline, then adapt to ROOMSTATE slow mode.

    Put the limiter above IrcClient so everything goes through it: await send_chat(channel, text)
   and await send_reply(channel, parent_id, text) acquire permits then call irc_client_.reply(...).

    Queue and drip messages rather than dropping, with a small bounded backlog.
*/
