// C++ Standard Library
#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

// App
#include <app/control_commands.hpp>

namespace app {

using twitch_bot::IrcMessage;

static std::string canonical(std::string_view s)
{
    // strip leading '#', lowercase ASCII
    if (!s.empty() && s.front() == '#') {
        s.remove_prefix(1);
    }
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        out.push_back((c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : char(c));
    }
    return out;
}

void control_commands(twitch_bot::TwitchBot& bot, ChannelStore& store)
{
    auto& dispatcher_ = bot.dispatcher();

    // ---------- !join ---------------------------------------------------------
    dispatcher_.register_command(
        "join", [&bot, &store](IrcMessage msg) noexcept -> boost::asio::awaitable<void> {
            const auto channel = msg.params[0];
            const auto user = msg.prefix;
            const auto args = msg.trailing;
            const std::string_view parent_id = msg.get_tag("id");

            if (channel != bot.control_channel()) {
                co_return;
            }

            if (!args.empty() && !bot.is_privileged(msg)) {
                std::string warn;
                warn.reserve(128 + user.size());
                warn.append("@")
                    .append(user)
                    .append(" You must be a mod to invite the bot to a different channel. ")
                    .append("Use !join from your own channel instead.");
                co_await bot.reply(channel, parent_id, warn);
                co_return;
            }

            std::string target = canonical(args.empty() ? user : args);

            if (store.contains(target)) {
                std::string s = "Already in channel " + target;
                co_await bot.reply(channel, parent_id, s);
                co_return;
            }

            store.add_channel(target);
            store.save();
            co_await bot.join_channel(target);

            std::string ack = "Joined " + target;
            co_await bot.reply(channel, parent_id, ack);
        });

    // ---------- !leave --------------------------------------------------------
    dispatcher_.register_command(
        "leave", [&bot, &store](IrcMessage msg) noexcept -> boost::asio::awaitable<void> {
            const auto channel = msg.params[0];
            const auto user = msg.prefix;
            const auto args = msg.trailing;
            const std::string_view parent_id = msg.get_tag("id");

            if (channel != bot.control_channel()) {
                co_return;
            }

            if (!args.empty() && !bot.is_privileged(msg)) {
                std::string warn;
                warn.reserve(120 + user.size());
                warn.append("@")
                    .append(user)
                    .append(" You must be a mod to remove the bot from another channel. ")
                    .append("Use !leave from your own channel instead.");
                co_await bot.reply(channel, parent_id, warn);
                co_return;
            }

            std::string target = canonical(args.empty() ? user : args);

            if (!store.contains(target)) {
                std::string s = "Not in channel " + target;
                co_await bot.reply(channel, parent_id, s);
                co_return;
            }

            store.remove_channel(target);
            store.save();
            co_await bot.part_channel(target);

            std::string ack = "Left " + target;
            co_await bot.reply(channel, parent_id, ack);
        });

    // ---------- !channels -----------------------------------------------------
    dispatcher_.register_command(
        "channels", [&bot, &store](IrcMessage msg) noexcept -> boost::asio::awaitable<void> {
            const auto channel = msg.params[0];
            if (channel != bot.control_channel()) {
                co_return;
            }

            std::vector<std::string> names;
            store.channel_names(names);

            std::string list;
            for (std::size_t i = 0; i < names.size(); ++i) {
                list += names[i];
                if (i + 1 < names.size()) {
                    list += ", ";
                }
            }
            if (list.empty()) {
                list = "(none)";
            }

            co_await bot.say(channel, "Currently in channels: " + list);
        });
}

} // namespace app
