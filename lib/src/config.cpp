#include "config.hpp"

#include <utility>

#include <toml++/toml.hpp>

namespace {

/// Central parsing routine: throws EnvError on any failure.
env::Config parseConfig(const std::filesystem::path& path) {
    using namespace std::literals;
    const std::string pathStr = path.string();

    toml::table tbl;
    try {
        tbl = toml::parse_file(pathStr);
    }
    catch (const toml::parse_error& e) {
        throw env::EnvError{"TOML parse error in '"s + pathStr + "': "s + e.what()};
    }
    catch (const std::filesystem::filesystem_error& e) {
        throw env::EnvError{"Cannot read config '"s + pathStr + "': "s + e.what()};
    }

    // Helper that drills into tbl via a list of keys.
    auto fetchString = [&](std::initializer_list<std::string_view> keys) -> std::string {
        const toml::node* node = &tbl;
        for (auto key : keys) {
            // Each step must be a table
            auto tblPtr = node->as_table();
            if (!tblPtr) {
                throw env::EnvError{"Expected table at '" + std::string(key) + "' in " + pathStr};
            }
            auto found = tblPtr->get(key);
            if (!found) {
                // build a dotted key path for the error
                std::string dotted;
                for (auto k : keys) {
                    if (!dotted.empty()) dotted += '.';
                    dotted += k;
                }
                throw env::EnvError{"Missing key '" + dotted + "' in " + pathStr};
            }
            node = found;
        }
        if (auto opt = node->value<std::string>()) {
            return *opt;
        }
        throw env::EnvError{"Value at requested key is not a string in " + pathStr};
    };

    return {
        fetchString({"twitch", "chat", "oauth_token"}),
        fetchString({"twitch", "chat", "refresh_token"}),
        fetchString({"twitch", "app",  "client_id"}),
        fetchString({"twitch", "app",  "client_secret"}),
        fetchString({"twitch", "bot",  "channel"})
    };
}

} // namespace

namespace env {

EnvError::EnvError(std::string msg) noexcept
    : std::runtime_error(std::move(msg))
{}

Config Config::loadFile(const std::filesystem::path& path) {
    return parseConfig(path);
}

Config Config::load() {
    // Always look in the current working directory
    const std::filesystem::path p =
        std::filesystem::current_path() / "config.toml";

    if (!std::filesystem::exists(p)) {
        throw EnvError{
            "Config file not found in current directory: '" + p.string() + "'"
        };
    }

    return parseConfig(p);
}

} // namespace env
