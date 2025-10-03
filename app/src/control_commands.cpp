/*
Module: control_commands.cpp

Purpose:
- Register app-layer admin/channel commands on the bot's dispatcher:
    !join [channel]     -> add to persisted set and JOIN
    !leave [channel]    -> remove from set and PART
    !channels           -> list all channels currently persisted
- Provide simple operational controls from the control channel.

Why:
- The bot needs runtime chat commands to manage which channels it is in,
  without rebuilding/redeploying. This module wires those commands to the
  core (TwitchBot) and persistence (ChannelStore).

Notes:
- Security: commands are only honored when issued from the configured
  control channel (exact match). Targeting a *different* channel requires
  privilege (broadcaster/mod/admin per TwitchBot::is_privileged).
- Normalization: channel names are canonicalized by stripping a leading '#'
  and lowercasing ASCII before comparing/persisting (aligns with Twitch).
- Persistence: ChannelStore::save() is debounced internally; we call it
  after mutations to ensure the on-disk TOML is eventually consistent.
*/

// C++ Standard Library
#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

// App
#include <app/control_commands.hpp>

namespace app
{

    using twitch_bot::IrcMessage;

    // Canonicalize a channel/login string:
    // - strip optional leading '#'
    // - lowercase ASCII (Twitch logins are case-insensitive)
    static std::string canonical(std::string_view s)
    {
        if (!s.empty() && s.front() == '#')
        {
            s.remove_prefix(1);
        }
        std::string out;
        out.reserve(s.size());
        for (unsigned char c : s)
        {
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
                const auto channel = msg.params[0]; // control channel (no '#')
                const auto user = msg.prefix; // login part of prefix
                const auto args = msg.trailing; // optional explicit target
                const std::string_view parent_id = msg.get_tag("id");

                // Only handle requests coming from the configured control channel.
                if (channel != bot.control_channel())
                {
                    co_return;
                }

                // Non-privileged users may only invite the bot to their own channel.
                if (!args.empty() && !bot.is_privileged(msg))
                {
                    std::string warn;
                    warn.reserve(128 + user.size());
                    warn.append("@")
                        .append(user)
                        .append(" You must be a mod to invite the bot to a different channel. ")
                        .append("Use !join from your own channel instead.");
                    co_await bot.reply(channel, parent_id, warn);
                    co_return;
                }

                // Resolve target: explicit arg or caller's login; Normalise either way.
                std::string target = canonical(args.empty() ? user : args);

                if (store.contains(target))
                {
                    std::string s = "Already in channel " + target;
                    co_await bot.reply(channel, parent_id, s);
                    co_return;
                }

                // Persist intent then join; save() is debounced internally.
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

                // Only handle requests coming from the configured control channel.
                if (channel != bot.control_channel())
                {
                    co_return;
                }

                // Non-privileged users may only remove the bot from their own channel.
                if (!args.empty() && !bot.is_privileged(msg))
                {
                    std::string warn;
                    warn.reserve(120 + user.size());
                    warn.append("@")
                        .append(user)
                        .append(" You must be a mod to remove the bot from another channel. ")
                        .append("Use !leave from your own channel instead.");
                    co_await bot.reply(channel, parent_id, warn);
                    co_return;
                }

                // Resolve target: explicit arg or caller's login; Normalise either way.
                std::string target = canonical(args.empty() ? user : args);

                if (!store.contains(target))
                {
                    std::string s = "Not in channel " + target;
                    co_await bot.reply(channel, parent_id, s);
                    co_return;
                }

                // Persist removal then part; save() is debounced internally.
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

                // Only respond in the control channel to avoid spam elsewhere.
                if (channel != bot.control_channel())
                {
                    co_return;
                }

                // Snapshot current list; ChannelStore provides lowercase names.
                std::vector<std::string> names;
                store.channel_names(names);

                // Render a compact, comma-separated list.
                std::string list;
                for (std::size_t i = 0; i < names.size(); ++i)
                {
                    list += names[i];
                    if (i + 1 < names.size())
                    {
                        list += ", ";
                    }
                }
                if (list.empty())
                {
                    list = "(none)";
                }

                co_await bot.say(channel, "Currently in channels: " + list);
            });
    }

} // namespace app
