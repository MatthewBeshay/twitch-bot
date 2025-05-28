#include "config.hpp"
#include "twitch_bot.hpp"
#include <iostream>
#include <cstdlib>

int main() {
    try {
        auto cfg = env::Config::load();

        TwitchBot bot{
            /* oauthToken      = */ cfg.twitchChatOauthToken_,
            /* clientId        = */ cfg.twitchAppClientId_,
            /* clientSecret    = */ cfg.twitchAppClientSecret_,
            /* controlChannel  = */ cfg.twitchBotChannel_,
            /* faceitApiKey    = */ cfg.faceitApiKey_
        };

        bot.run();
    }
    catch (const env::EnvError& ex) {
        std::cerr << "Configuration error: " << ex.what() << "\n"
            << "Please set the required environment variables "
            "(in the OS or in .env).\n";
        return EXIT_FAILURE;
    }
    catch (const std::exception& ex) {
        std::cerr << "Fatal startup error: " << ex.what() << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
