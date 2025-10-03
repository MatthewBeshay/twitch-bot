#pragma once

/*
Module: register_integrations.hpp

Purpose:
- Declare a single wiring point that adds app-layer commands which depend on
  Integrations and per-channel app state.

Why:
- Keep the TwitchBot core transport-agnostic and free of provider-specific
  behaviour. Centralising this registration keeps the surface clean and makes
  main() straightforward.

Notes:
- No ownership is transferred. The caller must keep bot, integrations and store
  alive for at least as long as the registered handlers can run.
- Handlers are scheduled on the bot's executor and must be non-blocking.
*/

// Core
#include <tb/twitch/twitch_bot.hpp>

// App
#include <app/app_channel_store.hpp>
#include <app/integrations.hpp>

namespace app
{

    // Register app-layer commands that use Integrations and per-channel app state.
    void register_integrations(twitch_bot::TwitchBot& bot,
                               const app::Integrations& integrations,
                               app::AppChannelStore& store);

} // namespace app
