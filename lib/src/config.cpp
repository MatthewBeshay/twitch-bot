// C++ Standard Library
#include <initializer_list>
#include <string_view>
#include <utility>

// 3rd-party
#include <toml++/toml.hpp>

// Project
#include "config.hpp"

namespace env {

// Read, validate and convert the TOML file at path.
auto Config::parse_config(const std::filesystem::path& path) -> Config
{
    const auto path_str = path.string();
    toml::table tbl;

    try {
        tbl = toml::parse_file(path_str);
    } catch (const toml::parse_error& e) {
        std::string err = "TOML parse error in '" + path_str + "': " + e.what();
        throw EnvError(err);
    } catch (const std::filesystem::filesystem_error& e) {
        std::string err = "Cannot read config file '" + path_str + "': " + e.what();
        throw EnvError(err);
    }

    // Return a non-empty string at dotted key path.
    auto fetch_string = [&](std::initializer_list<std::string_view> keys) -> std::string {
        const toml::node* node = &tbl;

        for (auto key_part : keys) {
            const auto* table_ptr = node->as_table();
            if (!table_ptr)
                throw EnvError("Expected table at '" + std::string{key_part} + "' in " + path_str);

            if (const auto* found = table_ptr->get(key_part))
                node = found;
            else
                throw EnvError("Missing key '" + std::string{key_part} + "' in " + path_str);
        }

        if (auto opt = node->value<std::string>(); opt && !opt->empty())
            return *opt;

        throw EnvError("Invalid value in " + path_str);
    };

    ChatConfig chat_cfg{.oauth_token = fetch_string({"twitch", "chat", "oauth_token"}),
                        .refresh_token = fetch_string({"twitch", "chat", "refresh_token"})};

    AppConfig app_cfg{.client_id = fetch_string({"twitch", "app", "client_id"}),
                      .client_secret = fetch_string({"twitch", "app", "client_secret"})};

    BotConfig bot_cfg{fetch_string({"twitch", "bot", "channel"})};

    FaceitConfig faceit_cfg{fetch_string({"faceit", "api_key"})};

    return Config(std::move(chat_cfg), std::move(app_cfg), std::move(bot_cfg),
                  std::move(faceit_cfg));
}

auto Config::load_file(const std::filesystem::path& path) -> Config
{
    if (path.string().empty())
        throw EnvError("Config file path must not be empty");

    return parse_config(path);
}

auto Config::load() -> Config
{
    const auto default_path = std::filesystem::current_path() / "config.toml";
    if (!std::filesystem::exists(default_path))
        throw EnvError("Config file not found at '" + default_path.string() + "'");

    return parse_config(default_path);
}

EnvError::EnvError(const std::string& msg) noexcept : std::runtime_error{std::move(msg)}
{
}

} // namespace env
