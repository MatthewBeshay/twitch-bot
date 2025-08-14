#pragma once

// C++ Standard Library
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace env {

/// Configuration-loading failure.
class EnvError final : public std::runtime_error
{
public:
    explicit EnvError(const std::string& msg) noexcept;
};

/// Twitch application credentials.
struct AppConfig {
    std::string client_id;
    std::string client_secret;
};

/// Twitch bot identity and control channel.
struct BotConfig {
    std::string login; ///< bot username (lowercase)
    std::string control_channel; ///< defaults to login if not set
};

/// Twitch OAuth tokens.
struct AuthConfig {
    std::string access_token;
    std::string refresh_token;
};

/// Immutable application configuration (single TOML file).
class Config
{
public:
    /// Load from the file at \p path.
    /// Pre: !path.empty()
    static Config load_file(const std::filesystem::path& path);

    /// Load from "./config.toml".
    static Config load();

    [[nodiscard]] const AppConfig& app() const noexcept
    {
        return app_;
    }
    [[nodiscard]] const BotConfig& bot() const noexcept
    {
        return bot_;
    }
    [[nodiscard]] const AuthConfig& auth() const noexcept
    {
        return auth_;
    }
    /// Absolute path to the loaded config file (useful for persistence).
    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    static Config parse_config(const std::filesystem::path& path);

    Config(std::filesystem::path path,
           AppConfig app_cfg,
           BotConfig bot_cfg,
           AuthConfig auth_cfg) noexcept
        : path_{std::move(path)}
        , app_{std::move(app_cfg)}
        , bot_{std::move(bot_cfg)}
        , auth_{std::move(auth_cfg)}
    {
    }

    std::filesystem::path path_;
    AppConfig app_;
    BotConfig bot_;
    AuthConfig auth_;
};

/// Overwrite twitch.chat.access_token in the given config file.
/// Returns true on success, false on failure (keeps file unchanged on exception).
bool write_access_token_in_config(const std::filesystem::path& path,
                                  std::string_view new_access_token) noexcept;

} // namespace env
