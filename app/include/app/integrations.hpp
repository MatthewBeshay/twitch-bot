#pragma once

// C++ Standard Library
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace app {

// Simple error for app-side config loading.
class EnvError final : public std::runtime_error
{
public:
    explicit EnvError(const std::string& msg) noexcept : std::runtime_error{msg}
    {
    }
};

// Load integration credentials from app_config.toml,
// with environment-variable overrides.
// Env precedence (case-insensitive service/key):
//   1) INTEGRATIONS_<SERVICE>_<KEY>
//   2) <SERVICE>_<KEY>
//   3) If key == "api_key": <SERVICE>_API_KEY
class Integrations
{
public:
    [[nodiscard]] static Integrations load(); // from ./app_config.toml
    [[nodiscard]] static Integrations load_file(const std::filesystem::path&); // explicit file

    [[nodiscard]] bool has(std::string_view service) const noexcept;

    // Throws EnvError if missing in both env and file.
    [[nodiscard]] std::string get(std::string_view service, std::string_view key) const;

    [[nodiscard]] std::string api_key(std::string_view service) const
    {
        return get(service, "api_key");
    }

    [[nodiscard]] std::optional<std::string> get_opt(std::string_view service,
                                                     std::string_view key) const;
    [[nodiscard]] std::optional<std::string> api_key_opt(std::string_view service) const
    {
        return get_opt(service, "api_key");
    }

    // Returns merged (env overrides file) key/values for a service.
    [[nodiscard]] std::unordered_map<std::string, std::string>
    values(std::string_view service) const;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    using KV = std::unordered_map<std::string, std::string>;
    using Map = std::unordered_map<std::string, KV>; // service -> (key -> value)

    [[nodiscard]] static Integrations parse_file(const std::filesystem::path&);

    // ASCII helpers (no locale).
    [[nodiscard]] static std::string to_lower_ascii(std::string_view);
    [[nodiscard]] static std::optional<std::string> env_override(std::string_view service,
                                                                 std::string_view key);

    explicit Integrations(std::filesystem::path p, Map m) noexcept
        : path_{std::move(p)}, data_{std::move(m)}
    {
    }

    std::filesystem::path path_;
    Map data_;
};

} // namespace app
