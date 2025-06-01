#pragma once

#include "config_path.hpp"

#include <string>
#include <filesystem>
#include <stdexcept>

namespace env {

/// Exception thrown on any error loading or parsing the config file.
class EnvError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Application configuration loaded from a TOML file.
struct Config {
    std::string twitchChatOauthToken_;   ///< Twitch chat OAuth token
    std::string twitchChatRefreshToken_; ///< Twitch chat refresh token
    std::string twitchAppClientId_;      ///< Twitch App client ID
    std::string twitchAppClientSecret_;  ///< Twitch App client secret
    std::string twitchBotChannel_;       ///< Control channel for the bot

    /**
     * @brief Load all variables from the given TOML file.
     * @param tomlFilePath  Path to the TOML file (e.g. "config.toml").
     * @return A fully populated Config.
     * @throws EnvError on missing file, parse error, or missing key.
     */
    static Config load(const std::filesystem::path& tomlFilePath);

    /**
     * @brief Auto-locate the TOML file (CWD first, then fallback) and load it.
     * @return A fully populated Config.
     * @throws EnvError on missing file, parse error, or missing key.
     */
    static Config load();
};

} // namespace env
