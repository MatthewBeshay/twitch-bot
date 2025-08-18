// C++ Standard Library
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

// Toml++
#include <toml++/toml.hpp>

// App
#include <app/integrations.hpp>

namespace app {
namespace {

    // ASCII predicates/transforms (no locale).
    constexpr bool is_ascii_alnum(unsigned char c) noexcept
    {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    }
    constexpr char ascii_toupper(unsigned char c) noexcept
    {
        return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : static_cast<char>(c);
    }
    constexpr char ascii_tolower(unsigned char c) noexcept
    {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c);
    }

#ifdef _WIN32
    // MSVC-safe getenv (avoids C4996).
    static std::optional<std::string> getenv_nonempty(const char* name) noexcept
    {
        if (!name) {
            return std::nullopt;
        }
        char* buf = nullptr;
        size_t len = 0;
        if (_dupenv_s(&buf, &len, name) != 0 || !buf) {
            return std::nullopt;
        }
        std::string out{buf};
        free(buf);
        if (out.empty()) {
            return std::nullopt;
        }
        return out;
    }
#else
    static std::optional<std::string> getenv_nonempty(const char* name) noexcept
    {
        if (!name) {
            return std::nullopt;
        }
        if (const char* v = std::getenv(name); v && *v) {
            return std::string(v);
        }
        return std::nullopt;
    }
#endif

} // namespace

Integrations Integrations::load()
{
    const auto default_path = std::filesystem::current_path() / "app_config.toml";
    if (!std::filesystem::exists(default_path)) {
        throw EnvError("Integrations: file not found at '" + default_path.string() + "'");
    }
    return parse_file(default_path);
}

Integrations Integrations::load_file(const std::filesystem::path& path)
{
    if (path.empty()) {
        throw EnvError("Integrations: path must not be empty");
    }
    if (!std::filesystem::exists(path)) {
        throw EnvError("Integrations: file not found at '" + path.string() + "'");
    }
    return parse_file(path);
}

bool Integrations::has(std::string_view service) const noexcept
{
    const auto s = to_lower_ascii(service);
    return data_.find(s) != data_.end();
}

std::string Integrations::get(std::string_view service, std::string_view key) const
{
    if (auto e = env_override(service, key)) {
        return *e;
    }

    const auto s = to_lower_ascii(service);
    const auto it = data_.find(s);
    if (it == data_.end()) {
        throw EnvError("Integrations: missing service '" + std::string(service) + "'");
    }

    const auto& kv = it->second;
    const auto kit = kv.find(std::string(key));
    if (kit == kv.end() || kit->second.empty()) {
        throw EnvError("Integrations: missing key '" + std::string(key) + "' for service '"
                       + std::string(service) + "'");
    }
    return kit->second;
}

std::optional<std::string> Integrations::get_opt(std::string_view service,
                                                 std::string_view key) const
{
    if (auto e = env_override(service, key)) {
        return e;
    }

    const auto s = to_lower_ascii(service);
    const auto it = data_.find(s);
    if (it == data_.end()) {
        return std::nullopt;
    }

    const auto& kv = it->second;
    const auto kit = kv.find(std::string(key));
    if (kit == kv.end() || kit->second.empty()) {
        return std::nullopt;
    }
    return kit->second;
}

std::unordered_map<std::string, std::string> Integrations::values(std::string_view service) const
{
    const auto s = to_lower_ascii(service);
    const auto it = data_.find(s);
    if (it == data_.end()) {
        return {};
    }

    auto out = it->second; // copy file values
    for (auto& [k, v] : out) {
        if (auto e = env_override(service, k)) {
            v = std::move(*e);
        }
    }
    return out;
}

Integrations Integrations::parse_file(const std::filesystem::path& path)
{
    toml::table tbl;
    try {
        tbl = toml::parse_file(path.string());
    } catch (const toml::parse_error& e) {
        throw EnvError("Integrations: TOML parse error in '" + path.string()
                       + "': " + std::string{e.what()});
    } catch (const std::filesystem::filesystem_error& e) {
        throw EnvError("Integrations: cannot read file '" + path.string()
                       + "': " + std::string{e.what()});
    }

    Map map;

    if (auto* integrations = tbl.get_as<toml::table>("integrations")) {
        map.reserve(integrations->size());
        for (auto&& [svc_key, svc_node] : *integrations) {
            if (!svc_node.is_table()) {
                continue;
            }
            const auto svc_name = to_lower_ascii(svc_key.str());

            KV kv;
            if (const auto* t = svc_node.as_table()) {
                kv.reserve(t->size());
                for (auto&& [k, v] : *t) {
                    if (auto sval = v.value<std::string>(); sval.has_value()) {
                        kv.emplace(k.str(), std::move(*sval));
                    }
                }
            }
            if (!kv.empty()) {
                map.emplace(svc_name, std::move(kv));
            }
        }
    }

    return Integrations(path, std::move(map));
}

std::string Integrations::to_lower_ascii(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        out.push_back(ascii_tolower(c));
    }
    return out;
}

std::optional<std::string> Integrations::env_override(std::string_view service,
                                                      std::string_view key)
{
    const auto mk = [](std::string_view a, std::string_view b, std::string_view c) {
        std::string s;
        s.reserve(a.size() + b.size() + c.size() + 2);
        const auto add = [&s](std::string_view t) {
            for (unsigned char ch : t) {
                s.push_back(is_ascii_alnum(ch) ? ascii_toupper(ch) : '_');
            }
        };
        add(a);
        if (!b.empty()) {
            s.push_back('_');
            add(b);
        }
        if (!c.empty()) {
            s.push_back('_');
            add(c);
        }
        return s;
    };

    const auto primary = mk("INTEGRATIONS", service, key);
    if (auto v = getenv_nonempty(primary.c_str())) {
        return v;
    }

    const auto fallback1 = mk(service, key, "");
    if (auto v = getenv_nonempty(fallback1.c_str())) {
        return v;
    }

    if (key == "api_key") {
        const auto fallback2 = mk(service, "API_KEY", "");
        if (auto v = getenv_nonempty(fallback2.c_str())) {
            return v;
        }
    }
    return std::nullopt;
}

} // namespace app
