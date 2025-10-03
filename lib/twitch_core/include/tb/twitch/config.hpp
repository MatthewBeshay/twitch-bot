/*
Module Name:
- config.hpp

Abstract:
- Immutable configuration for the Twitch bot loaded from a single TOML file.
- Surfaces strongly typed sections (app, bot, auth) and the absolute file path.
- Fails fast with EnvError on invalid or missing configuration.
- Includes a helper to update the access token on disk without changing other fields.
*/
#pragma once

// C++ Standard Library
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace env
{

    /// Configuration-loading failure. Prefer specific errors over generic runtime_error.
    class EnvError final : public std::runtime_error
    {
    public:
        explicit EnvError(const std::string& msg) noexcept;
    };

    /// Twitch application credentials.
    struct AppConfig
    {
        std::string client_id;
        std::string client_secret;
    };

    /// Twitch bot identity and control channel.
    /// control_channel defaults to login if not set in the file.
    struct BotConfig
    {
        std::string login; ///< bot username (lowercase)
        std::string control_channel; ///< defaults to login if not set
    };

    /// Twitch OAuth tokens.
    struct AuthConfig
    {
        std::string access_token;
        std::string refresh_token;
    };

    /// Immutable application configuration (single TOML file).
    class Config
    {
    public:
        /// Load from the file at path.
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
        /// Absolute path to the loaded config file. Useful for later persistence.
        [[nodiscard]] const std::filesystem::path& path() const noexcept
        {
            return path_;
        }

    private:
        static Config parse_config(const std::filesystem::path& path);

        // Store is immutable after construction to keep call sites simple and thread friendly.
        Config(std::filesystem::path path,
               AppConfig app_cfg,
               BotConfig bot_cfg,
               AuthConfig auth_cfg) noexcept
            :
            path_{ std::move(path) }, app_{ std::move(app_cfg) }, bot_{ std::move(bot_cfg) }, auth_{ std::move(auth_cfg) }
        {
        }

        std::filesystem::path path_;
        AppConfig app_;
        BotConfig bot_;
        AuthConfig auth_;
    };

    /// Overwrite twitch.chat.access_token in the given config file.
    /// Returns true on success, false on failure. Implementations should avoid
    /// partial updates so the file remains valid if an error occurs.
    bool write_access_token_in_config(const std::filesystem::path& path,
                                      std::string_view new_access_token) noexcept;

} // namespace env
