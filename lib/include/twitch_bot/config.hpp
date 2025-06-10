#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>

namespace env {

/// Error loading or parsing the config file.
class EnvError final : public std::runtime_error {
public:
    explicit EnvError(std::string msg) noexcept;
};

/// Application settings loaded from TOML. Immutable once constructed.
struct Config {
    std::string twitchChatOauthToken;
    std::string twitchChatRefreshToken;
    std::string twitchAppClientId;
    std::string twitchAppClientSecret;
    std::string twitchBotChannel;

    /// Load from the given file. Throws EnvError on any problem.
    static Config loadFile(const std::filesystem::path& path);

    /// Load `./config.toml` from CWD.
    static Config load();
};

} // namespace env
