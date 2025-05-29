#include "config.hpp"
#include <toml++/toml.hpp>
#include <sstream>
#include <filesystem>

namespace {

/// Join ["twitch","chat","oauth_token"] -> "twitch.chat.oauth_token"
std::string join_keys(std::initializer_list<std::string_view> keys) {
    std::string out;
    bool first = true;
    for (auto k : keys) {
        if (!first) out += '.';
        out += k;
        first = false;
    }
    return out;
}

/// Look up a string at the dotted path in tbl, or throw env::EnvError.
std::string fetch_string(const toml::table& tbl,
                         const std::filesystem::path& path,
                         std::initializer_list<std::string_view> keys)
{
    auto dotted = join_keys(keys);
    auto node_v = tbl.at_path(dotted);
    if (auto opt = node_v.template value<std::string>(); opt)
        return *opt;

    throw env::EnvError{
        "Missing or invalid key [" + dotted + "] in " + path.string()
    };
}

} // anonymous

namespace env {

Config Config::load(const std::filesystem::path& tomlFilePath)
{
    toml::table tbl;

    // Convert path -> UTF-8 string so parse_file() will match the char8_t overload
    auto u8path = tomlFilePath.u8string();  // std::basic_string<char8_t>

#if TOML_EXCEPTIONS
    // Exception-based parsing
    try {
        tbl = toml::parse_file(u8path);
    }
    catch (const toml::parse_error& err) {
        std::ostringstream oss;
        oss << "TOML parse error in '" << tomlFilePath
            << "': " << err;  // full error + location
        throw EnvError{oss.str()};
    }
    catch (const std::exception& err) {
        throw EnvError{
            "Failed to load '" + tomlFilePath.string() + "': " + err.what()
        };
    }
#else
    // No-exceptions parsing
    auto result = toml::parse_file(u8path);
    if (!result) {
        std::ostringstream oss;
        oss << "TOML parse error in '" << tomlFilePath
            << "': " << result.error();
        throw EnvError{oss.str()};
    }
    tbl = *result;  // unwrap the table
#endif

    // Extract each required field (throws EnvError if missing/invalid)
    Config cfg;
    cfg.faceitApiKey_           = fetch_string(tbl, tomlFilePath, {"faceit_api_key"});
    cfg.twitchChatOauthToken_   = fetch_string(tbl, tomlFilePath, {"twitch","chat","oauth_token"});
    cfg.twitchChatRefreshToken_ = fetch_string(tbl, tomlFilePath, {"twitch","chat","refresh_token"});
    cfg.twitchAppClientId_      = fetch_string(tbl, tomlFilePath, {"twitch","app","client_id"});
    cfg.twitchAppClientSecret_  = fetch_string(tbl, tomlFilePath, {"twitch","app","client_secret"});
    cfg.twitchBotChannel_       = fetch_string(tbl, tomlFilePath, {"twitch","bot","channel"});

    return cfg;
}

} // namespace env
