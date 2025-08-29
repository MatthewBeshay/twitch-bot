#pragma once

// C++ Standard Library
#include <chrono>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Project
#include "cookie.hpp"

namespace tb::net
{

    class CookieJar
    {
    public:
        CookieJar() = default;

        // Store/replace a cookie
        void store(const Cookie& c);
        void store(Cookie&& c);

        // Overloads expected by http_client: allow providing defaults/context.
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

        // Parse and store from a Set-Cookie header line
        void store_from_set_cookie(std::string_view set_cookie_line,
                                   std::string_view default_domain,
                                   std::string_view default_path,
                                   bool from_https,
                                   std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

        // Build the Cookie: request header for a given request
        std::string cookie_header_for(std::string_view host,
                                      std::string_view path,
                                      bool is_https,
                                      std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

        // Return matching cookies
        std::vector<Cookie> matching(std::string_view host,
                                     std::string_view path,
                                     bool is_https,
                                     std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

        // Drop expired cookies
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
        // domain -> bag of cookies (exact-domain matching)
        std::unordered_map<std::string, std::vector<Cookie>> by_domain_;

        static bool path_match(std::string_view req_path, std::string_view cookie_path) noexcept;

        static bool domain_match(std::string_view host, std::string_view cookie_domain) noexcept;

        static void upsert(std::vector<Cookie>& vec, Cookie&& c);
        static void erase_exact(std::vector<Cookie>& vec, std::string_view name, std::string_view path);

        // Normalise a cookie with provided defaults
        static Cookie normalise(Cookie c,
                                std::string_view default_domain,
                                std::string_view default_path,
                                bool /*from_https*/);
    };

} // namespace tb::net
