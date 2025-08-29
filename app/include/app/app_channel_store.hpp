#pragma once

// C++ Standard Library
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace app
{

    // Not thread-safe. Lower-cases channel keys.
    class AppChannelStore
    {
    public:
        explicit AppChannelStore(std::filesystem::path path = "app_channels.toml") noexcept
            :
            path_(std::move(path))
        {
        }

        void load();
        void save() const noexcept;

        [[nodiscard]] const std::filesystem::path& path() const noexcept
        {
            return path_;
        }

        [[nodiscard]] bool contains(std::string_view channel) const noexcept;
        [[nodiscard]] std::optional<std::string> get(std::string_view channel) const noexcept;

        void set(std::string_view channel, std::string value) noexcept;
        void erase(std::string_view channel) noexcept;

    private:
        static std::string to_lower_ascii(std::string_view s);

        std::filesystem::path path_;
        std::unordered_map<std::string, std::string> per_channel_;
    };

} // namespace app
