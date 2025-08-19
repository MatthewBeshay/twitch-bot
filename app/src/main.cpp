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
    try {
        const auto cfg = env::Config::load();
        const auto config_path = cfg.path();

        twitch_bot::TwitchBot bot{cfg.auth().access_token, cfg.auth().refresh_token,
                                  cfg.app().client_id, cfg.app().client_secret,
                                  cfg.bot().control_channel};

        // Persist refreshed access tokens back to config (best-effort).
        bot.helix().set_access_token_persistor([config_path](std::string_view tok) {
            (void)env::write_access_token_in_config(config_path, tok);
        });

        app::ChannelStore channels{bot.executor(), "channels.toml"};
        channels.load();

        {
            std::vector<std::string> initial;
            channels.channel_names(initial);
            bot.set_initial_channels(std::move(initial));
        }

        app::control_commands(bot, channels);

        const auto integrations = app::Integrations::load();
        app::AppChannelStore app_chan_store{"app_channels.toml"};
        app_chan_store.load();
        app::register_integrations(bot, integrations, app_chan_store);

        bot.run();
    } catch (const env::EnvError& e) {
        std::cerr << "Configuration error: " << e.what() << '\n';
        return EXIT_FAILURE;
    } catch (const std::exception& e) {
        std::cerr << "Fatal startup error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }

#ifndef NDEBUG
    std::cerr << "\nPress Enter to exit...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
#endif

    return EXIT_SUCCESS;
}
