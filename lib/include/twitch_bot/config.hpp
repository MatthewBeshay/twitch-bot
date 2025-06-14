#pragma once

// C++ Standard Library
#include <filesystem>
#include <stdexcept>
#include <string>

namespace env {

/// Thrown when loading or parsing the configuration fails.
class EnvError final : public std::runtime_error
{
public:
    explicit EnvError(const std::string& msg) noexcept;
};

/// Application configuration loaded from a TOML file.
struct Config {
    std::string twitchChatOauthToken_;   ///< Twitch chat OAuth token
    std::string twitchChatRefreshToken_; ///< Twitch chat refresh token
    std::string twitchAppClientId_;      ///< Twitch App client ID
    std::string twitchAppClientSecret_;  ///< Twitch App client secret
    std::string twitchBotChannel_;       ///< Control channel for the bot
    std::string faceitApiKey_;           ///< FACEIT API key

    /**
     * @brief Load all variables from the given TOML file.
     * @param tomlFilePath  Path to the TOML file (e.g. "config.toml").
     * @return A fully populated Config.
     * @throws EnvError on missing file, parse error, or missing key.
     */
    static Config load(const std::filesystem::path& tomlFilePath);

    /// Load configuration from "./config.toml" in working directory.
    /// @throws EnvError on failure
    static Config load();

    /// Return Twitch chat credentials.
    const ChatConfig &chat() const noexcept
    {
        return chat_;
    }

    /// Return Twitch application credentials.
    const AppConfig &app() const noexcept
    {
        return app_;
    }

    /// Return Twitch bot settings.
    const BotConfig &bot() const noexcept
    {
        return bot_;
    }

private:
    Config(ChatConfig chat_cfg, AppConfig app_cfg, BotConfig bot_cfg) noexcept
        : chat_{std::move(chat_cfg)}, app_{std::move(app_cfg)}, bot_{std::move(bot_cfg)}
    {
    }

    ChatConfig chat_;
    AppConfig app_;
    BotConfig bot_;

    static Config parse_config(const std::filesystem::path &path);
};

} // namespace env
