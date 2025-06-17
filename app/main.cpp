#include "config.hpp"
#include "twitch_bot.hpp"

#include <iostream>
#include <cstdlib>
#include <limits>

int main() {
    try {
        // Load the immutable application configuration
        auto cfg = env::Config::load();

        // Unpack only the values TwitchBot actually needs
        twitch_bot::TwitchBot bot{
            /* oauthToken     = */ cfg.chat().oauth_token,
            /* clientId       = */ cfg.app().client_id,
            /* clientSecret   = */ cfg.app().client_secret,
            /* controlChannel = */ cfg.bot().channel,
            /* faceitApiKey   = */ cfg.faceit().api_key
        };

        bot.run();
    }
    catch (const env::EnvError& e) {
        std::cerr << "Configuration error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal startup error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }

#ifndef NDEBUG
    // In debug builds, wait for the user to hit Enter before closing
    std::cerr << "\nPress Enter to exit...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
#endif

    return EXIT_SUCCESS;
}
