#pragma once

// Core
#include <tb/twitch/twitch_bot.hpp>

// App
#include <app/channel_store.hpp>

namespace app {

// Register admin/channel commands in the app layer:
// - !join [channel]
// - !leave [channel]
// - !channels
void control_commands(twitch_bot::TwitchBot& bot, ChannelStore& store);

} // namespace app
