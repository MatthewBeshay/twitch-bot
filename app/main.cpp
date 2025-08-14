// C++ Standard Library
#include <cstdlib>
#include <iostream>
#include <limits>

// Project
#include <tb/twitch/config.hpp>
#include <tb/twitch/twitch_bot.hpp>

int main()
{
    try {
        // Load config
        const auto cfg = env::Config::load();
        const auto config_path = cfg.path();

        // Build the bot
        twitch_bot::TwitchBot bot{/* accessToken    = */ cfg.auth().access_token,
                                  /* refreshToken   = */ cfg.auth().refresh_token,
                                  /* clientId       = */ cfg.app().client_id,
                                  /* clientSecret   = */ cfg.app().client_secret,
                                  /* controlChannel = */ cfg.bot().control_channel};

        // Persist new ACCESS tokens back into config.toml when we refresh
        bot.helix().set_access_token_persistor([config_path](std::string_view tok) {
            const bool ok = env::write_access_token_in_config(config_path, tok);
            if (!ok) {
                std::cerr << "[warn] failed to persist access_token to " << config_path.string()
                          << '\n';
            }
        });

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
