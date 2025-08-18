#pragma once

// Core
#include <tb/twitch/twitch_bot.hpp>

// App
#include <app/app_channel_store.hpp>
#include <app/integrations.hpp>

namespace app {

// Register app-layer commands that use Integrations and per-channel app state.
void register_integrations(twitch_bot::TwitchBot& bot,
                           const app::Integrations& integrations,
                           app::AppChannelStore& store);

} // namespace app
