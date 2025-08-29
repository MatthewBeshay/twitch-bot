// C++ Standard Library
#include <fstream>
#include <initializer_list>
#include <string_view>
#include <utility>

// TOML++
#include <toml++/toml.hpp>

// Project
#include <tb/twitch/config.hpp>

namespace env
{

    namespace
    {
        // Return a non-empty string at dotted key path or throw EnvError.
        std::string fetch_string(const toml::table& root,
                                 std::initializer_list<std::string_view> keys,
                                 const std::string& path_str)
        {
            const toml::node* node = &root;
            for (auto key : keys)
            {
                const auto* table_ptr = node->as_table();
                if (!table_ptr)
                    throw EnvError("Expected table while reading '" + path_str + "'");
                if (const auto* found = table_ptr->get(key))
                    node = found;
                else
                    throw EnvError("Missing key '" + std::string{ key } + "' in " + path_str);
            }

            if (auto opt = node->value<std::string>(); opt && !opt->empty())
                return *opt;

            throw EnvError("Invalid value in " + path_str);
        }

        // Return optional string if present and non-empty.
        std::string fetch_optional_string(const toml::table& root,
                                          std::initializer_list<std::string_view> keys)
        {
            const toml::node* node = &root;
            for (auto key : keys)
            {
                const auto* table_ptr = node->as_table();
                if (!table_ptr)
                    return {};
                if (const auto* found = table_ptr->get(key))
                    node = found;
                else
                    return {};
            }
            if (auto opt = node->value<std::string>(); opt && !opt->empty())
                return *opt;
            return {};
        }
    } // namespace

    // Read, validate and convert the TOML file at path.
    Config Config::parse_config(const std::filesystem::path& path)
    {
        const auto path_str = path.string();
        toml::table tbl;

        try
        {
            tbl = toml::parse_file(path_str);
        }
        catch (const toml::parse_error& e)
        {
            throw EnvError("TOML parse error in '" + path_str + "': " + std::string{ e.what() });
        }
        catch (const std::filesystem::filesystem_error& e)
        {
            throw EnvError("Cannot read config file '" + path_str + "': " + std::string{ e.what() });
        }

        AppConfig app_cfg{
            .client_id = fetch_string(tbl, { "twitch", "app", "client_id" }, path_str),
            .client_secret = fetch_string(tbl, { "twitch", "app", "client_secret" }, path_str),
        };

        BotConfig bot_cfg{
            .login = fetch_string(tbl, { "twitch", "bot", "login" }, path_str),
            .control_channel = {}, // filled below (defaults to login)
        };
        {
            auto cc = fetch_optional_string(tbl, { "twitch", "bot", "control_channel" });
            bot_cfg.control_channel = cc.empty() ? bot_cfg.login : std::move(cc);
        }

        AuthConfig auth_cfg{
            .access_token = fetch_string(tbl, { "twitch", "auth", "access_token" }, path_str),
            .refresh_token = fetch_string(tbl, { "twitch", "auth", "refresh_token" }, path_str),
        };

        return Config(path, std::move(app_cfg), std::move(bot_cfg), std::move(auth_cfg));
    }

    Config Config::load_file(const std::filesystem::path& path)
    {
        if (path.string().empty())
            throw EnvError("Config file path must not be empty");
        return parse_config(path);
    }

    Config Config::load()
    {
        const auto default_path = std::filesystem::current_path() / "config.toml";
        if (!std::filesystem::exists(default_path))
            throw EnvError("Config file not found at '" + default_path.string() + "'");
        return parse_config(default_path);
    }

    EnvError::EnvError(const std::string& msg) noexcept :
        std::runtime_error{ msg }
    {
    }

    bool write_access_token_in_config(const std::filesystem::path& path,
                                      std::string_view new_access_token) noexcept
    {
        try
        {
            auto tbl = toml::parse_file(path.string());

            auto set_token = [&](toml::table& auth) {
                (void)auth.insert_or_assign("access_token", std::string(new_access_token));
            };

            if (auto* tw = tbl.get_as<toml::table>("twitch"))
            {
                if (auto* auth = tw->get_as<toml::table>("auth"))
                {
                    set_token(*auth);
                }
                else
                {
                    toml::table auth_tbl;
                    set_token(auth_tbl);
                    tw->insert("auth", std::move(auth_tbl));
                }
            }
            else
            {
                toml::table auth_tbl;
                set_token(auth_tbl);

                toml::table tw_tbl;
                tw_tbl.insert("auth", std::move(auth_tbl));
                tbl.insert("twitch", std::move(tw_tbl));
            }

            std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
            if (!ofs)
                return false;
            ofs << tbl;
            return ofs.good();
        }
        catch (...)
        {
            return false;
        }
    }

} // namespace env
