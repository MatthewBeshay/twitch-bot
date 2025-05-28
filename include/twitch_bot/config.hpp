#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace env {

/// @brief Thrown on any error loading or parsing the env file.
class EnvError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// @brief Application configuration loaded from a “.env” file.
/// 
/// Holds FACEIT and Twitch credentials and settings.
struct Config {
    std::string faceitApiKey_;           ///< FACEIT API key (v4)
    std::string twitchChatOauthToken_;   ///< Twitch chat OAuth token
    std::string twitchChatRefreshToken_; ///< Twitch chat refresh token
    std::string twitchAppClientId_;      ///< Twitch App client ID
    std::string twitchAppClientSecret_;  ///< Twitch App client secret
    std::string twitchBotChannel_;       ///< Control channel for the bot

    /// @brief Load all variables from the given env file.
    /// @param envFilePath Path to the “.env” file (defaults to “.env” in CWD).
    /// @return A fully populated Config instance.
    /// @throws EnvError if the file is missing, unreadable, or a required key is absent.
    [[nodiscard]]
    static Config load(std::filesystem::path const& envFilePath = ".env");
};

} // namespace env
