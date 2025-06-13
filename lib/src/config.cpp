#include "config.hpp"

#include <initializer_list>
#include <string_view>
#include <utility>

#include <toml++/toml.hpp>

namespace env {

Config Config::parse_config(const std::filesystem::path& path)
{
    const auto path_str = path.string();
    toml::table tbl;

    try {
        tbl = toml::parse_file(path_str);
    }
    catch (const toml::parse_error& e) {
        throw EnvError("TOML parse error in '" + path_str
                       + "': " + e.what());
    }
    catch (const std::filesystem::filesystem_error& e) {
        throw EnvError("Cannot read config file '" + path_str
                       + "': " + e.what());
    }

    // Fetch a non-empty string at the given key path
    auto fetch_string =
        [&](std::initializer_list<std::string_view> keys) -> std::string
    {
        const toml::node* node = &tbl;

        for (auto key : keys) {
            auto table_ptr = node->as_table();
            if (!table_ptr) {
                throw EnvError("Expected table at '" + std::string{key}
                               + "' in " + path_str);
            }

            auto found = table_ptr->get(key);
            if (!found) {
                std::string dotted;
                for (auto k : keys) {
                    if (!dotted.empty()) {
                        dotted += '.';
                    }
                    dotted += k;
                }
                throw EnvError("Missing key '" + dotted
                               + "' in " + path_str);
            }

            node = found;
        }

        if (auto opt = node->value<std::string>()) {
            if (opt->empty()) {
                throw EnvError("Empty value in " + path_str);
            }
            return *opt;
        }

        throw EnvError("Non-string value in " + path_str);
    };

    ChatConfig chat_cfg {
        fetch_string({"twitch", "chat", "oauth_token"}),
        fetch_string({"twitch", "chat", "refresh_token"})
    };

    AppConfig app_cfg {
        fetch_string({"twitch", "app", "client_id"}),
        fetch_string({"twitch", "app", "client_secret"})
    };

    BotConfig bot_cfg {
        fetch_string({"twitch", "bot", "channel"})
    };

    return Config(std::move(chat_cfg),
                  std::move(app_cfg),
                  std::move(bot_cfg));
}

Config Config::load_file(const std::filesystem::path& path)
{
    if (path.string().empty()) {
        throw EnvError("Config file path must not be empty");
    }
    return parse_config(path);
}

Config Config::load()
{
    const auto default_path =
        std::filesystem::current_path() / "config.toml";

    if (!std::filesystem::exists(default_path)) {
        throw EnvError("Config file not found at '"
                       + default_path.string() + "'");
    }

    return parse_config(default_path);
}

EnvError::EnvError(std::string msg) noexcept
    : std::runtime_error{std::move(msg)}
{
}

} // namespace env
