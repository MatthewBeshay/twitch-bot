#pragma once

// C++ Standard Library
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace app {

// Minimal per-channel settings for the FACEIT integration.
struct FaceitSettings {
    std::optional<std::string> nickname;
    std::optional<std::string> player_id;
};

// Not thread-safe. Channel keys are normalized to lowercase.
class AppChannelStore
{
public:
    explicit AppChannelStore(std::filesystem::path path = "app_channels.toml") noexcept;

    // Read from disk if the file exists; tolerates parse errors.
    void load() noexcept;

    // Write current state back to disk; best-effort.
    void save() const noexcept;

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    [[nodiscard]] bool contains(std::string_view channel) const noexcept;
    void erase(std::string_view channel) noexcept;

    // FACEIT helpers
    [[nodiscard]] std::optional<std::string> get_faceit_nick(std::string_view channel) const;
    [[nodiscard]] std::optional<std::string> get_faceit_id  (std::string_view channel) const;

    void set_faceit_nick(std::string_view channel, std::string nick);
    void set_faceit_id  (std::string_view channel, std::string id);
    void clear_faceit_id(std::string_view channel);

private:
    static std::string to_lower_ascii(std::string_view s);

    std::filesystem::path path_;
    std::unordered_map<std::string, FaceitSettings> per_channel_; // lowercase key
};

} // namespace app
