#pragma once

#include "config_path.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>

namespace env {

/// Exception thrown on any error loading or parsing the config file.
class EnvError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Application configuration loaded from a TOML file.
struct Config {
    std::string twitchChatOauthToken;   // Twitch chat OAuth token
    std::string twitchChatRefreshToken; // Twitch chat refresh token
    std::string twitchAppClientId;      // Twitch App client ID
    std::string twitchAppClientSecret;  // Twitch App client secret
    std::string twitchBotChannel;       // Control channel for the bot

    /// Load all variables from the given TOML file.
    /// @param tomlFilePath  Path to the TOML file (for example, "config.toml").
    /// @return A fully populated Config.
    /// @throws EnvError on missing file, parse error, or missing key.
    static Config load(const std::filesystem::path& tomlFilePath);

    /// Auto-locate the TOML file (current working directory first, then fallback) and load it.
    /// @return A fully populated Config.
    /// @throws EnvError on missing file, parse error, or missing key.
    static Config load();
};

} // namespace env
