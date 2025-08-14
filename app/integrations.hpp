#pragma once

// C++ Standard Library
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

// Project
#include <tb/twitch/config.hpp>

namespace env {

class Integrations
{
public:
    // Load from "./config.toml".
    [[nodiscard]] static Integrations load();

    // Load from an explicit file path.
    [[nodiscard]] static Integrations load_file(const std::filesystem::path& path);

    // True if we have an entry for this service (case-insensitive).
    [[nodiscard]] bool has(std::string_view service) const noexcept;

    // Get a specific key for a service, honoring env overrides.
    // Throws EnvError if not present anywhere.
    [[nodiscard]] std::string get(std::string_view service, std::string_view key) const;

    // Convenience: fetch the common "api_key" field.
    [[nodiscard]] std::string api_key(std::string_view service) const
    {
        return get(service, "api_key");
    }

    // Optional getter (returns std::nullopt if missing).
    [[nodiscard]] std::optional<std::string> get_opt(std::string_view service,
                                                     std::string_view key) const;

    // Optional getter for api_key.
    [[nodiscard]] std::optional<std::string> api_key_opt(std::string_view service) const
    {
        return get_opt(service, "api_key");
    }

    // Return all key/values for a service (TOML values as strings), after env overrides.
    [[nodiscard]] std::unordered_map<std::string, std::string>
    values(std::string_view service) const;

    // Path the config was loaded from.
    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    using KV = std::unordered_map<std::string, std::string>;
    using Map = std::unordered_map<std::string, KV>; // service -> (key -> value)

    [[nodiscard]] static Integrations parse_file(const std::filesystem::path& path);

    // ASCII-only transforms (avoid locale pitfalls).
    [[nodiscard]] static std::string to_lower_ascii(std::string_view s);
    [[nodiscard]] static std::optional<std::string> env_override(std::string_view service,
                                                                 std::string_view key);

    explicit Integrations(std::filesystem::path p, Map m) noexcept
        : path_{std::move(p)}, data_{std::move(m)}
    {
    }

    std::filesystem::path path_;
    Map data_;
};

} // namespace env
