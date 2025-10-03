#pragma once

/*
Module: control_commands.hpp

Purpose:
- Declare registration of app-level admin commands on the bot.

Why:
- Keep command wiring in the app layer so the core bot stays generic.
- Centralises admin controls that mutate channel membership using the shared store.

Commands:
- !join [channel]     - join and persist intent
- !leave [channel]    - part and clear persisted intent
- !channels           - list persisted channels
*/

// Core
#include <tb/twitch/twitch_bot.hpp>

// App
#include <app/channel_store.hpp>

namespace app
{

    // Register admin and channel-management commands on the given bot.
    // Handlers use ChannelStore to persist intent across reconnects.
    void control_commands(twitch_bot::TwitchBot& bot, ChannelStore& store);

} // namespace app
