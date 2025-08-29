// C++ Standard Library
#include <algorithm>
#include <utility>

// Project
#include <tb/net/http/cookie_jar.hpp>

namespace tb::net
{

    bool CookieJar::path_match(std::string_view req_path, std::string_view cookie_path) noexcept
    {
        if (cookie_path.empty())
        {
            cookie_path = "/";
        }
        if (req_path.empty())
        {
            req_path = "/";
        }

        if (req_path.size() < cookie_path.size())
        {
            return false;
        }
        if (req_path.substr(0, cookie_path.size()) != cookie_path)
        {
            return false;
        }
        return (req_path.size() == cookie_path.size()) || (cookie_path.back() == '/' || req_path[cookie_path.size()] == '/');
    }

    bool CookieJar::domain_match(std::string_view host, std::string_view cookie_domain) noexcept
    {
        // Strict host-only match for now.
        return host == cookie_domain;
    }

    void CookieJar::upsert(std::vector<Cookie>& vec, Cookie&& c)
    {
        auto it = std::find_if(vec.begin(), vec.end(), [&](const Cookie& x) { return x.name == c.name && x.path == c.path; });
        if (it != vec.end())
        {
            *it = std::move(c);
        }
        else
        {
            vec.emplace_back(std::move(c));
        }
    }

    void CookieJar::erase_exact(std::vector<Cookie>& vec, std::string_view name, std::string_view path)
    {
        vec.erase(std::remove_if(vec.begin(),
                                 vec.end(),
                                 [&](const Cookie& x) { return x.name == name && x.path == path; }),
                  vec.end());
    }

    Cookie CookieJar::normalise(Cookie c,
                                std::string_view default_domain,
                                std::string_view default_path,
                                bool /*from_https*/)
    {
        if (c.domain.empty())
        {
            c.domain = std::string(default_domain);
        }
        if (c.path.empty())
        {
            c.path = default_path.empty() ? std::string("/") : std::string(default_path);
        }

        return c;
    }

    void CookieJar::store(const Cookie& c)
    {
        auto& bag = by_domain_[c.domain];
        upsert(bag, Cookie{ c });
    }

    void CookieJar::store(Cookie&& c)
    {
        auto& bag = by_domain_[c.domain];
        upsert(bag, std::move(c));
    }

    void CookieJar::store(const Cookie& c,
                          std::string_view default_domain,
                          std::string_view default_path,
                          bool from_https,
                          std::chrono::system_clock::time_point now)
    {
        Cookie nc = normalise(c, default_domain, default_path, from_https);
        if (nc.expired_at(now))
        {
            auto it = by_domain_.find(nc.domain);
            if (it != by_domain_.end())
            {
                erase_exact(it->second, nc.name, nc.path);
                if (it->second.empty())
                {
                    by_domain_.erase(it);
                }
            }
            return;
        }
        store(std::move(nc));
    }

    void CookieJar::store(Cookie&& c,
                          std::string_view default_domain,
                          std::string_view default_path,
                          bool from_https,
                          std::chrono::system_clock::time_point now)
    {
        Cookie nc = normalise(std::move(c), default_domain, default_path, from_https);
        if (nc.expired_at(now))
        {
            auto it = by_domain_.find(nc.domain);
            if (it != by_domain_.end())
            {
                erase_exact(it->second, nc.name, nc.path);
                if (it->second.empty())
                {
                    by_domain_.erase(it);
                }
            }
            return;
        }
        store(std::move(nc));
    }

    void CookieJar::store_from_set_cookie(std::string_view set_cookie_line,
                                          std::string_view default_domain,
                                          std::string_view default_path,
                                          bool from_https,
                                          std::chrono::system_clock::time_point now)
    {
        auto parsed = parse_set_cookie(set_cookie_line, default_domain, default_path, from_https);
        if (!parsed)
            return;

        Cookie c = std::move(*parsed);
        if (c.expired_at(now))
        {
            auto it = by_domain_.find(c.domain);
            if (it != by_domain_.end())
            {
                erase_exact(it->second, c.name, c.path);
                if (it->second.empty())
                {
                    by_domain_.erase(it);
                }
            }
            return;
        }
        store(std::move(c));
    }

    std::vector<Cookie> CookieJar::matching(std::string_view host,
                                            std::string_view path,
                                            bool is_https,
                                            std::chrono::system_clock::time_point now)
    {
        std::vector<Cookie> out;

        auto it = by_domain_.find(std::string(host));
        if (it == by_domain_.end())
        {
            return out;
        }

        const auto& bag = it->second;
        out.reserve(bag.size());

        for (const auto& c : bag)
        {
            if (c.secure && !is_https)
            {
                continue;
            }
            if (c.expired_at(now))
            {
                continue;
            }
            if (!domain_match(host, c.domain))
            {
                continue;
            }
            if (!path_match(path, c.path))
            {
                continue;
            }

            out.push_back(c);
        }

        std::sort(out.begin(), out.end(), [](const Cookie& a, const Cookie& b) { return a.path.size() > b.path.size(); });

        return out;
    }

    std::string CookieJar::cookie_header_for(std::string_view host,
                                             std::string_view path,
                                             bool is_https,
                                             std::chrono::system_clock::time_point now)
    {
        auto vec = matching(host, path, is_https, now);
        return build_cookie_header(vec);
    }

    void CookieJar::purge_expired(std::chrono::system_clock::time_point now)
    {
        for (auto it = by_domain_.begin(); it != by_domain_.end();)
        {
            auto& bag = it->second;
            bag.erase(std::remove_if(bag.begin(), bag.end(), [&](const Cookie& c) { return c.expired_at(now); }),
                      bag.end());
            if (bag.empty())
            {
                it = by_domain_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

} // namespace tb::net
