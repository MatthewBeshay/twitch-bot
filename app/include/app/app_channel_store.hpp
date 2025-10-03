#pragma once

// App-level channel store (non-concurrent).
// Why:
// - Persist a per-channel string value (e.g. alias) in a tiny TOML file.
// - Normalise keys to lowercase for consistent lookups (Twitch-style, ASCII only).
// - Intentionally not thread-safe: expected to be used from a single UI/logic thread.

// C++ Standard Library
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace app
{

    class AppChannelStore
    {
    public:
        // Default to "app_channels.toml" next to the binary.
        explicit AppChannelStore(std::filesystem::path path = "app_channels.toml") noexcept
            :
            path_(std::move(path))
        {
        }

        // Load from disk if the file exists; leaves map unchanged on parse errors.
        void load();

        // Best-effort save to disk; swallows I/O errors.
        void save() const noexcept;

        [[nodiscard]] const std::filesystem::path& path() const noexcept
        {
            return path_;
        }

        // Case-insensitive w.r.t. ASCII by normalising keys to lowercase.
        [[nodiscard]] bool contains(std::string_view channel) const noexcept;

        // Returns a copy to avoid dangling if the store mutates after return.
        [[nodiscard]] std::optional<std::string> get(std::string_view channel) const noexcept;

        // Insert or replace the value for channel (key is lowercased ASCII).
        void set(std::string_view channel, std::string value) noexcept;

        // Erase if present (key is lowercased ASCII).
        void erase(std::string_view channel) noexcept;

    private:
        // Locale-free ASCII lowering (by design, Twitch channel names are ASCII-lower).
        static std::string to_lower_ascii(std::string_view s);

        std::filesystem::path path_;
        std::unordered_map<std::string, std::string> per_channel_; // key: lowercase channel
    };

} // namespace app
