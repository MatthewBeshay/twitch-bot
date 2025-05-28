// config.hpp
#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace env {

    struct EnvError : std::runtime_error {
        using std::runtime_error::runtime_error;
    };

    struct Config {
        std::string faceitApiKey;
        std::string twitchChatOauthToken;
        std::string twitchChatRefreshToken;
        std::string twitchAppClientId;
        std::string twitchAppClientSecret;
        std::string twitchBotChannel;

        [[nodiscard]]
        static Config load(const std::filesystem::path& envFilePath = ".env");
    };

} // namespace env
