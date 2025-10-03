#pragma once

/*
Module: integrations.hpp

Purpose:
- Define a tiny credentials loader for app integrations.

Why:
- Keep secrets out of the repo by reading a local TOML file, but allow easy
  overrides from environment variables for CI and prod.
- Use a clear precedence so operators know exactly which source wins.
- Keep the type small and header-only friendly for callers that just need keys.

Env precedence (case-insensitive on service/key):
  1) INTEGRATIONS_<SERVICE>_<KEY>  - namespaced to avoid clashes across apps
  2) <SERVICE>_<KEY>               - conventional fallback for simple setups
  3) Special case: if key == "api_key" also accept <SERVICE>_API_KEY
Notes:
- ASCII-only case folding is deliberate. It avoids locale surprises and matches
  the env-var character set you are likely to use in practice.
- This class does not do live reloading. Load once at startup and pass around.
*/

// C++ Standard Library
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>

namespace app
{

    // Simple typed error so callers can catch configuration issues explicitly.
    class EnvError final : public std::runtime_error
    {
    public:
        explicit EnvError(const std::string& msg) noexcept :
            std::runtime_error{ msg }
        {
        }
    };

    // Loader that merges app_config.toml with environment overrides.
    // TOML file shape (example):
    //
    //   [openai]
    //   api_key = "sk-..."
    //   org = "org_123"
    //
    //   [github]
    //   token = "ghp_..."
    //
    // Environment examples that would override the above:
    //   INTEGRATIONS_OPENAI_API_KEY=sk-from-env
    //   OPENAI_ORG=org_from_env
    //
    // Keep this type value-like and cheap to pass by const reference.
    class Integrations
    {
    public:
        [[nodiscard]] static Integrations load(); // from ./app_config.toml
        [[nodiscard]] static Integrations load_file(const std::filesystem::path&); // explicit file

        [[nodiscard]] bool has(std::string_view service) const noexcept;

        // Throws EnvError if the key is missing in both env and file.
        [[nodiscard]] std::string get(std::string_view service, std::string_view key) const;

        // Convenience for the common case.
        [[nodiscard]] std::string api_key(std::string_view service) const
        {
            return get(service, "api_key");
        }

        // Optional lookups when a missing value is acceptable.
        [[nodiscard]] std::optional<std::string> get_opt(std::string_view service,
                                                         std::string_view key) const;
        [[nodiscard]] std::optional<std::string> api_key_opt(std::string_view service) const
        {
            return get_opt(service, "api_key");
        }

        // Returns the merged view for a service so callers can inspect all keys.
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

        // ASCII-only helpers by design. Env vars are typically ASCII, and this
        // avoids locale-dependent behaviour.
        [[nodiscard]] static std::string to_lower_ascii(std::string_view);
        [[nodiscard]] static std::optional<std::string> env_override(std::string_view service,
                                                                     std::string_view key);

        explicit Integrations(std::filesystem::path p, Map m) noexcept
            :
            path_{ std::move(p) }, data_{ std::move(m) }
        {
        }

        std::filesystem::path path_;
        Map data_;
    };

} // namespace app
