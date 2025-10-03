/*
Module Name:
- cookie.hpp

Abstract:
- RFC 6265 style cookie model and small helpers for HTTP clients.
- Stores attributes and expiry so callers can filter before sending.
- Defaults choose safer behaviour: path "/" and http_only true.
*/
#pragma once

// C++ Standard Library
#include <cctype>
#include <chrono>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tb::net
{

    // RFC 6265 SameSite values. kNull means attribute not present.
    enum class SameSite
    {
        kNull,
        kLax,
        kStrict,
        kNone
    };

    struct Cookie
    {
        std::string name;
        std::string value;

        // Attributes
        std::string domain;
        std::string path = "/"; // sensible default per common client practice
        bool secure = false;
        bool http_only = true; // default to not exposing to scripts
        bool partitioned = false;
        std::optional<int> max_age; // seconds
        std::optional<std::chrono::system_clock::time_point> expires;
        SameSite same_site = SameSite::kNull;

        Cookie() = default;
        Cookie(std::string n, std::string v) :
            name(std::move(n)), value(std::move(v))
        {
        }

        // True if the cookie should not be sent at 'now'.
        [[nodiscard]] bool expired_at(std::chrono::system_clock::time_point now) const noexcept
        {
            if (max_age && *max_age <= 0)
            {
                return true;
            }
            if (expires)
            {
                return now >= *expires;
            }
            return false;
        }
    };

    // Parse a single Set-Cookie header. Apply request-context defaults when
    // Domain or Path are absent. We store Secure cookies regardless of transport;
    // enforcement occurs when selecting for a request.
    std::optional<Cookie> parse_set_cookie(std::string_view set_cookie_line,
                                           std::string_view default_domain,
                                           std::string_view default_path,
                                           bool from_https);

    // Build the Cookie request header from preselected cookies.
    // Order follows the input span.
    std::string build_cookie_header(std::span<const Cookie> cookies);

} // namespace tb::net
