#pragma once

// C++ Standard Library
#include <cctype>
#include <chrono>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tb::net {

// RFC6265-ish flags
enum class SameSite { kNull, kLax, kStrict, kNone };

struct Cookie {
    std::string name;
    std::string value;

    // Attributes
    std::string domain;
    std::string path = "/";
    bool secure = false;
    bool http_only = true;
    bool partitioned = false;
    std::optional<int> max_age; // seconds
    std::optional<std::chrono::system_clock::time_point> expires;
    SameSite same_site = SameSite::kNull;

    Cookie() = default;
    Cookie(std::string n, std::string v) : name(std::move(n)), value(std::move(v))
    {
    }

    [[nodiscard]] bool expired_at(std::chrono::system_clock::time_point now) const noexcept
    {
        if (max_age && *max_age <= 0)
            return true;
        if (expires)
            return now >= *expires;
        return false;
    }
};

// Parse a single Set-Cookie header line into a Cookie.
// - default_domain/path are applied if missing in the header
// - from_https indicates whether the response arrived over HTTPS (affects
//   how you may later send the cookie, but we still store it here)
std::optional<Cookie> parse_set_cookie(std::string_view set_cookie_line,
                                       std::string_view default_domain,
                                       std::string_view default_path,
                                       bool from_https);

// Build a Cookie request header line from a list of cookies.
// (name=value; name2=value2)
std::string build_cookie_header(std::span<const Cookie> cookies);

} // namespace tb::net
