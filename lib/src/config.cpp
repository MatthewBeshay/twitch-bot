#include "config.hpp"

#include <filesystem>
#include <sstream>

#include <toml++/toml.hpp>

namespace {

/// Join ["twitch","chat","oauth_token"] → "twitch.chat.oauth_token"
std::string join_keys(std::initializer_list<std::string_view> keys) noexcept {
    std::string out;
    out.reserve(64);
    bool first = true;
    for (auto k : keys) {
        if (!first) out.push_back('.');
        out.append(k);
        first = false;
    }
    return out;
}

/// Fetch a string at a dotted‐path in the TOML table or throw EnvError.
std::string fetch_string(const toml::table& tbl,
                         const std::filesystem::path& path,
                         std::initializer_list<std::string_view> keys)
{
    auto dotted = join_keys(keys);
    auto node   = tbl.at_path(dotted);
    if (auto opt = node.template value<std::string>(); opt) {
        return *opt;
    }
    throw env::EnvError{
        "Missing or invalid key [" + dotted + "] in " + path.string()
    };
}

/// Parse the TOML file at @p path and return a populated Config.
/// @throws EnvError on parse failure or missing keys.
env::Config parseToml(const std::filesystem::path& path) {
    toml::table tbl;

#if TOML_EXCEPTIONS
    try {
        tbl = toml::parse_file(path.string());
    }
    catch (const toml::parse_error& err) {
        std::ostringstream oss;
        oss << "TOML parse error in '" << path << "': " << err;
        throw env::EnvError{oss.str()};
    }
    catch (const std::exception& ex) {
        throw env::EnvError{
            "Failed to load '" + path.string() + "': " + ex.what()
        };
    }
#else
    auto result = toml::parse_file(path.string());
    if (!result) {
        std::ostringstream oss;
        oss << "TOML parse error in '" << path << "': " << result.error();
        throw env::EnvError{oss.str()};
    }
    tbl = *result;
#endif

    env::Config cfg;
    cfg.twitchChatOauthToken_   = fetch_string(tbl, path, {"twitch","chat","oauth_token"});
    cfg.twitchChatRefreshToken_ = fetch_string(tbl, path, {"twitch","chat","refresh_token"});
    cfg.twitchAppClientId_      = fetch_string(tbl, path, {"twitch","app","client_id"});
    cfg.twitchAppClientSecret_  = fetch_string(tbl, path, {"twitch","app","client_secret"});
    cfg.twitchBotChannel_       = fetch_string(tbl, path, {"twitch","bot","channel"});
    return cfg;
}

/// Locate “config.toml”-check CWD first, then fallback to CMake‐baked path.
std::filesystem::path findConfigFile() noexcept {
    auto cwd       = std::filesystem::current_path();
    auto candidate = cwd / "config.toml";
    if (std::filesystem::exists(candidate)) {
        return candidate;
    }
    return getConfigPath();  // from config_path.hpp
}

} // anonymous namespace

namespace env {

Config Config::load(const std::filesystem::path& tomlFilePath) {
    if (!std::filesystem::exists(tomlFilePath)) {
        throw EnvError{
            "Config file not found: " + tomlFilePath.string()
        };
    }
    return parseToml(tomlFilePath);
}

Config Config::load() {
    return load(findConfigFile());
}

} // namespace env
