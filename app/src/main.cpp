/*
Module: main.cpp

Purpose:
- Entry point that wires up configuration, persistent stores, integrations,
  and the Twitch bot, then runs the event loop.

Why:
- Centralise all app bootstrapping so the rest of the codebase can stay
  focused on testable components (stores, clients, commands).

Notes:
- Config is loaded from ./config.toml (see env::Config). Fails fast with EnvError.
- Access tokens refreshed by Helix are persisted back into the same config file
  (best-effort; failure is non-fatal).
- Channel membership is loaded from channels.toml and applied before connect.
- App-layer commands are registered from control_commands and register_integrations.
- bot.run() blocks until the underlying IO context stops.
- In debug builds, we pause for Enter to keep console output visible.
*/

// C++ Standard Library
#include <cstdlib>
#include <iostream>
#include <limits>

// Core
#include <tb/twitch/config.hpp>
#include <tb/twitch/twitch_bot.hpp>

// App
#include <app/app_channel_store.hpp>
#include <app/channel_store.hpp>
#include <app/control_commands.hpp>
#include <app/integrations.hpp>
#include <app/register_integrations.hpp>

int main()
{
    try
    {
        // 1) Load immutable configuration (app creds, bot identity, tokens).
        const auto cfg = env::Config::load();
        const auto config_path = cfg.path();

        // 2) Construct the bot with initial credentials.
        twitch_bot::TwitchBot bot{
            cfg.auth().access_token,
            cfg.auth().refresh_token,
            cfg.app().client_id,
            cfg.app().client_secret,
            cfg.bot().control_channel
        };

        // 3) Persist refreshed access tokens back to config (best-effort, non-fatal).
        bot.helix().set_access_token_persistor([config_path](std::string_view tok) {
            (void)env::write_access_token_in_config(config_path, tok);
        });

        // 4) Load persistent channel membership and feed into the bot.
        app::ChannelStore channels{ bot.executor(), "channels.toml" };
        channels.load();
        {
            std::vector<std::string> initial;
            channels.channel_names(initial);
            bot.set_initial_channels(std::move(initial));
        }

        // 5) Core admin/channel commands (join/leave/list).
        app::control_commands(bot, channels);

        // 6) App integrations and per-channel app state.
        const auto integrations = app::Integrations::load();
        app::AppChannelStore app_chan_store{ "app_channels.toml" };
        app_chan_store.load();
        app::register_integrations(bot, integrations, app_chan_store);

        // 7) Hand control to the bot: blocks until IO stops.
        bot.run();
    }
    catch (const env::EnvError& e)
    {
        std::cerr << "Configuration error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
    catch (const std::exception& e)
    {
        std::cerr << "Fatal startup error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }

#ifndef NDEBUG
    // Keep console window open for inspection in debug builds.
    std::cerr << "\nPress Enter to exit...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
#endif

    return EXIT_SUCCESS;
}
