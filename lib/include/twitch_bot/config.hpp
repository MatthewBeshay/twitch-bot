#pragma once

// C++ Standard Library
#include <filesystem>
#include <stdexcept>
#include <string>

namespace env {

/// Configuration-loading failure.
class EnvError final : public std::runtime_error
{
public:
    explicit EnvError(const std::string& msg) noexcept;
};

/// Twitch chat credentials.
struct ChatConfig {
    std::string oauth_token;
    std::string refresh_token;
};

/// Twitch application credentials.
struct AppConfig {
    std::string client_id;
    std::string client_secret;
};

/// Twitch bot settings.
struct BotConfig {
    std::string channel;
};

/// Faceit Data API key (non-empty string).
struct FaceitConfig {
    std::string api_key;
};

/// Immutable application configuration.
class Config
{
public:
    /// Load from the file at \p path.
    /// Pre: !path.empty()
    static Config load_file(const std::filesystem::path& path);

    /// Load from "./config.toml".
    static Config load();

    /// Twitch chat credentials.
    [[nodiscard]] const ChatConfig& chat() const noexcept
    {
        return chat_;
    }

    /// Twitch application credentials.
    [[nodiscard]] const AppConfig& app() const noexcept
    {
        return app_;
    }

    /// Twitch bot settings.
    [[nodiscard]] const BotConfig& bot() const noexcept
    {
        return bot_;
    }

    /// Return Faceit Data API settings.
    const FaceitConfig& faceit() const noexcept
    {
        return faceit_;
    }

private:
    Config(ChatConfig chat_cfg,
           AppConfig app_cfg,
           BotConfig bot_cfg,
           FaceitConfig faceit_cfg) noexcept
        : chat_{std::move(chat_cfg)}
        , app_{std::move(app_cfg)}
        , bot_{std::move(bot_cfg)}
        , faceit_{std::move(faceit_cfg)}
    {
    }

    ChatConfig chat_;
    AppConfig app_;
    BotConfig bot_;
    FaceitConfig faceit_;

    static Config parse_config(const std::filesystem::path& path);
};

} // namespace env
