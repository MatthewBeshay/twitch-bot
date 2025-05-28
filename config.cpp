// config.cpp
#include "config.hpp"

#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace env {
namespace {
    // Whitespace for trimming
    constexpr std::string_view kWhitespace = " \t\n\r\f\v";

    std::string_view trim(std::string_view sv) noexcept {
        size_t b = 0, e = sv.size();
        while (b < e && kWhitespace.find(sv[b]) != std::string_view::npos) ++b;
        while (e > b && kWhitespace.find(sv[e - 1]) != std::string_view::npos) --e;
        return sv.substr(b, e - b);
    }

    // Environment‐variable keys
    constexpr std::string_view kFaceitKey            = "FACEIT_DATA_API_KEY";
    constexpr std::string_view kTwitchTokenKey       = "TWITCH_CHAT_OAUTH_TOKEN";
    constexpr std::string_view kTwitchRefreshKey     = "TWITCH_CHAT_REFRESH_TOKEN";
    constexpr std::string_view kTwitchClientIdKey    = "TWITCH_APP_CLIENT_ID";
    constexpr std::string_view kTwitchClientSecretKey= "TWITCH_APP_CLIENT_SECRET";
    constexpr std::string_view kBotChannelKey        = "TWITCH_BOT_CHANNEL";
} // anonymous namespace

Config Config::load(const std::filesystem::path& envFilePath) {
    std::ifstream file(envFilePath, std::ios::binary | std::ios::ate);
    if (!file) {
        throw EnvError("Unable to open env file: " + envFilePath.string());
    }

    auto size = file.tellg();
    if (size < 0) {
        throw EnvError("Unable to determine size of env file: " + envFilePath.string());
    }
    file.seekg(0, std::ios::beg);

    std::string content;
    content.resize(static_cast<size_t>(size));
    if (!file.read(content.data(), size)) {
        throw EnvError("Error reading env file: " + envFilePath.string());
    }

    std::optional<std::string_view> faceit;
    std::optional<std::string_view> oauthToken;
    std::optional<std::string_view> refreshToken;
    std::optional<std::string_view> clientId;
    std::optional<std::string_view> clientSecret;
    std::optional<std::string_view> botChannel;

    std::string_view view{content};
    while (!view.empty()) {
        // Extract one line (handles \n, \r, or \r\n)
        auto pos  = view.find_first_of("\r\n");
        auto line = view.substr(0, pos);

        if (pos == std::string_view::npos) {
            view.remove_prefix(view.size());
        } else {
            size_t skip = 1;
            if (view[pos] == '\r' && pos + 1 < view.size() && view[pos + 1] == '\n')
                skip = 2;
            view.remove_prefix(pos + skip);
        }

        auto sv = trim(line);
        if (sv.empty() || sv.front() == '#')
            continue;

        auto eq = sv.find('=');
        if (eq == std::string_view::npos)
            continue;

        auto key = trim(sv.substr(0, eq));
        auto val = trim(sv.substr(eq + 1));

        // Strip surrounding quotes
        if (val.size() >= 2 &&
            ((val.front() == '"' && val.back() == '"') ||
             (val.front() == '\'' && val.back() == '\'')))
        {
            val = val.substr(1, val.size() - 2);
        }

        // Strip inline comments if unquoted
        if (!val.empty() && val.front() != '"' && val.front() != '\'') {
            if (auto cpos = val.find('#'); cpos != std::string_view::npos) {
                val = trim(val.substr(0, cpos));
            }
        }

        // Dispatch
        if      (key == kFaceitKey)            faceit      = val;
        else if (key == kTwitchTokenKey)       oauthToken  = val;
        else if (key == kTwitchRefreshKey)     refreshToken= val;
        else if (key == kTwitchClientIdKey)    clientId    = val;
        else if (key == kTwitchClientSecretKey)clientSecret= val;
        else if (key == kBotChannelKey)        botChannel  = val;
    }

    // Ensure every required key was found
    auto require = [&](auto const& opt, std::string_view name) -> std::string_view {
        if (!opt) {
            throw EnvError("Missing environment key: " + std::string{name});
        }
        return *opt;
    };

    return Config{
        .faceitApiKey           = std::string{ require(faceit,       kFaceitKey) },
        .twitchChatOauthToken   = std::string{ require(oauthToken,   kTwitchTokenKey) },
        .twitchChatRefreshToken = std::string{ require(refreshToken, kTwitchRefreshKey) },
        .twitchAppClientId      = std::string{ require(clientId,     kTwitchClientIdKey) },
        .twitchAppClientSecret  = std::string{ require(clientSecret, kTwitchClientSecretKey) },
        .twitchBotChannel       = std::string{ require(botChannel,   kBotChannelKey) }
    };
}

} // namespace env
