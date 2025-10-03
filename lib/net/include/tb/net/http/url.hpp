/*
Module Name:
- url.hpp

Abstract:
- Minimal URL parse and resolve helpers for an HTTP client.
- Resolve supports absolute, scheme-relative, absolute-path, and relative refs.
- Dot-segment removal is a lightweight normalisation for common cases.
- Query is stored with a leading '?' so target() can concatenate cheaply.
*/
#pragma once

// C++ Standard Library
#include <string>
#include <string_view>
#include <utility>

namespace tb::net
{

    struct Url
    {
        std::string scheme;
        std::string host;
        std::string port;
        std::string path;
        std::string query; // includes leading '?' when present

        [[nodiscard]] bool is_absolute() const noexcept
        {
            return !scheme.empty();
        }

        [[nodiscard]] std::string authority() const
        {
            std::string out = host;
            if (!port.empty())
            {
                out.push_back(':');
                out += port;
            }
            return out;
        }

        [[nodiscard]] std::string target() const
        {
            if (query.empty())
            {
                return path.empty() ? std::string{ "/" } : path;
            }
            std::string out = path.empty() ? std::string{ "/" } : path;
            out += query; // query already has leading '?'
            return out;
        }

        [[nodiscard]] std::string origin() const
        {
            std::string out = scheme;
            out += "://";
            out += authority();
            return out;
        }
    };

    inline Url parse_url(std::string_view s)
    {
        Url u;

        // scheme "://"
        auto pos = s.find("://");
        if (pos != std::string_view::npos)
        {
            u.scheme.assign(s.substr(0, pos));
            s.remove_prefix(pos + 3);
        }

        // authority
        if (!u.scheme.empty())
        {
            auto slash = s.find('/');
            std::string_view auth = (slash == std::string_view::npos) ? s : s.substr(0, slash);
            if (slash == std::string_view::npos)
            {
                s = "/";
            }
            else
            {
                s.remove_prefix(slash);
            }

            // split host[:port] using last ':'
            auto colon = auth.rfind(':');
            if (colon != std::string_view::npos)
            {
                u.host.assign(auth.substr(0, colon));
                u.port.assign(auth.substr(colon + 1));
            }
            else
            {
                u.host.assign(auth);
            }
        }

        // path and optional query (query kept with leading '?')
        auto q = s.find('?');
        if (q == std::string_view::npos)
        {
            u.path.assign(s.empty() ? "/" : std::string(s));
        }
        else
        {
            u.path.assign(std::string(s.substr(0, q)));
            u.query.assign(std::string(s.substr(q)));
        }

        if (u.path.empty() || u.path.front() != '/')
        {
            u.path.insert(u.path.begin(), '/');
        }
        return u;
    }

    inline Url resolve_url(const Url& base, std::string_view location)
    {
        // absolute
        if (location.find("://") != std::string_view::npos)
        {
            return parse_url(location);
        }

        // scheme-relative: "//host/..."
        if (location.rfind("//", 0) == 0)
        {
            Url out = parse_url(std::string(base.scheme) + ":" + std::string(location));
            return out;
        }

        Url out = base;

        // absolute-path
        if (!location.empty() && location.front() == '/')
        {
            out.path.assign(location.begin(), location.end());
            out.query.clear();
            return out;
        }

        // relative-path: trim to last '/', then append
        auto last_slash = out.path.rfind('/');
        if (last_slash == std::string::npos)
        {
            out.path = "/";
        }
        else
        {
            out.path.resize(last_slash + 1);
        }
        out.path.append(location);

        // normalise: remove "/./"
        for (;;)
        {
            auto i = out.path.find("/./");
            if (i == std::string::npos)
            {
                break;
            }
            out.path.erase(i, 2);
        }
        // normalise: collapse "/../"
        for (;;)
        {
            auto i = out.path.find("/../");
            if (i == std::string::npos)
            {
                break;
            }
            auto j = out.path.rfind('/', i - 1);
            if (j == std::string::npos)
            {
                out.path.erase(0, i + 3);
                break;
            }
            out.path.erase(j, i + 3 - j);
        }

        if (out.path.empty() || out.path.front() != '/')
        {
            out.path.insert(out.path.begin(), '/');
        }
        out.query.clear();
        return out;
    }

} // namespace tb::net
