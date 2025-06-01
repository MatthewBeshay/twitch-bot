#include "config.hpp"
#include "twitch_bot.hpp"

#include <iostream>
#include <cstdlib>
#include <limits>

int main() {
    int exitCode = EXIT_SUCCESS;

    try {
        auto cfg = env::Config::load();
        twitch_bot::TwitchBot bot{
            /* oauthToken     = */ cfg.twitchChatOauthToken_,
            /* clientId       = */ cfg.twitchAppClientId_,
            /* clientSecret   = */ cfg.twitchAppClientSecret_,
            /* controlChannel = */ cfg.twitchBotChannel_,
            /* faceitApiKey   = */ cfg.faceitApiKey_
        };
        bot.run();
    }
    catch (const env::EnvError& e) {
        std::cerr << "Configuration error: " << e.what() << "\n";
        exitCode = EXIT_FAILURE;
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal startup error: " << e.what() << "\n";
        exitCode = EXIT_FAILURE;
    }

  #ifndef NDEBUG
    // In debug builds, wait for the user to hit Enter before closing.
    std::cerr << "\nPress Enter to exit...";
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
  #endif

    return exitCode;
}
