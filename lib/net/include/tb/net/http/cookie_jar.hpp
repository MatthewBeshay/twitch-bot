/*
Module Name:
- cookie_jar.hpp

Abstract:
- RFC 6265 style client-side cookie store.
- Allows request-context defaults when Set-Cookie omits Domain or Path.
- Groups by exact domain for storage; selection applies RFC domain and path matching.
- Provides expiry eviction to avoid sending stale cookies.
*/
#pragma once

// C++ Standard Library
#include <chrono>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Core
#include "cookie.hpp"

namespace tb::net
{

    class CookieJar
    {
    public:
        CookieJar() = default;

        // Replace or add by (name, domain, path) identity.
        void store(const Cookie& c);
        void store(Cookie&& c);

        // Why: servers often omit Domain or Path. These overloads accept request
        // context so we can normalise and store consistently.
        void store(const Cookie& c,
                   std::string_view default_domain,
                   std::string_view default_path,
                   bool from_https,
                   std::chrono::system_clock::time_point now = std::chrono::system_clock::now());
        void store(Cookie&& c,
                   std::string_view default_domain,
                   std::string_view default_path,
                   bool from_https,
                   std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

        // Parse and store a single Set-Cookie header line using the same defaults.
        void store_from_set_cookie(std::string_view set_cookie_line,
                                   std::string_view default_domain,
                                   std::string_view default_path,
                                   bool from_https,
                                   std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

        // Build the Cookie request header for a target. Filters by host, path,
        // scheme, and expiry to avoid leaking or sending stale cookies.
        std::string cookie_header_for(std::string_view host,
                                      std::string_view path,
                                      bool is_https,
                                      std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

        // Return matching cookies for programmatic use. Same selection rules as above.
        std::vector<Cookie> matching(std::string_view host,
                                     std::string_view path,
                                     bool is_https,
                                     std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

        // Drop expired cookies to cap memory and network overhead.
        void purge_expired(std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

        // Aliases expected by http_client
        void evict_expired(std::chrono::system_clock::time_point now = std::chrono::system_clock::now())
        {
            purge_expired(now);
        }
        void clear() noexcept
        {
            by_domain_.clear();
        }

    private:
        // Exact-domain buckets; subdomain visibility is handled by domain_match.
        std::unordered_map<std::string, std::vector<Cookie>> by_domain_;

        // RFC 6265 path-match.
        static bool path_match(std::string_view req_path, std::string_view cookie_path) noexcept;

        // RFC 6265 domain-match, including host-only vs domain cookies.
        static bool domain_match(std::string_view host, std::string_view cookie_domain) noexcept;

        // Insert or replace by name+path within a domain bucket.
        static void upsert(std::vector<Cookie>& vec, Cookie&& c);
        static void erase_exact(std::vector<Cookie>& vec, std::string_view name, std::string_view path);

        // Apply defaults and normalise attributes for storage.
        static Cookie normalise(Cookie c,
                                std::string_view default_domain,
                                std::string_view default_path,
                                bool /*from_https*/);
    };

} // namespace tb::net
