#include "config.hpp"
#include "twitch_bot.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>

int main()
{
    try {
        auto cfg = env::Config::load();

        twitch_bot::TwitchBot bot{/* oauthToken     = */ cfg.chat().oauth_token,
                                  /* refreshToken   = */ cfg.chat().refresh_token,
                                  /* clientId       = */ cfg.app().client_id,
                                  /* clientSecret   = */ cfg.app().client_secret,
                                  /* controlChannel = */ cfg.bot().channel,
                                  /* faceitApiKey   = */ cfg.faceit().api_key};

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
