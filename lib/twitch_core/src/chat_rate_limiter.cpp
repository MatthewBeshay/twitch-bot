// C++ Standard Library
#include <algorithm>

// 3rd-party
#include <boost/asio/dispatch.hpp>
#include <boost/asio/use_awaitable.hpp>

// Project
#include <tb/twitch/chat_rate_limiter.hpp>

namespace twitch_bot {

using boost::asio::use_awaitable;

boost::asio::awaitable<void> ChatRateLimiter::send_privmsg(IrcClient& irc,
                                                           std::string_view channel,
                                                           std::string_view text) noexcept
{
    // Serialise rate-limit state
    co_await boost::asio::dispatch(strand_, use_awaitable);

    const auto now = clock::now();

    // Clean out global timestamps outside the window
    while (!global_sends_.empty() && now - global_sends_.front() > kGlobalWindow) {
        global_sends_.pop_front();
    }

    // Compute earliest permitted time due to per-channel gap
    clock::time_point ready_at = now;
    {
        auto it = next_per_channel_.find(channel);
        if (it != next_per_channel_.end() && it->second > ready_at) {
            ready_at = it->second;
        }
    }

    // If global bucket is full, delay until the oldest stamp rolls out of window
    if (static_cast<int>(global_sends_.size()) >= kGlobalBurst) {
        auto t = global_sends_.front() + kGlobalWindow;
        if (t > ready_at)
            ready_at = t;
    }

    // Wait if needed
    if (ready_at > now) {
        boost::asio::steady_timer t{strand_};
        t.expires_at(ready_at);
        co_await t.async_wait(use_awaitable);
    }

    // Build and send one IRC line (send_line adds CRLF)
    std::string line;
    line.reserve(10 + channel.size() + 2 + text.size());
    line.append("PRIVMSG #");
    line.append(channel);
    line.append(" :");
    line.append(text);

    co_await irc.send_line(line);

    // Update state after successful send
    const auto sent = clock::now();
    global_sends_.push_back(sent);
    next_per_channel_[std::string{channel}] = sent + kPerChannelGap;

    co_return;
}

} // namespace twitch_bot
