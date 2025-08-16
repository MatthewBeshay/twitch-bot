#pragma once

#include <string>
#include <string_view>
#include <utility>

namespace tb::net {

// Very small URL struct sufficient for http/https client work.
struct Url {
    std::string scheme; // "http" | "https" (lowercase)
    std::string host;   // reg-name or IP (as given)
    std::string port;   // empty = default (80/443)
    std::string path;   // always starts with '/' (at least "/")
    std::string query;  // includes leading '?' when non-empty

    bool is_absolute() const noexcept { return !scheme.empty(); }

    std::string authority() const {
        std::string out = host;
        if (!port.empty()) { out.push_back(':'); out += port; }
        return out;
    }

    std::string target() const {
        if (query.empty()) return path.empty() ? std::string{"/"} : path;
        std::string out = path.empty() ? std::string{"/"} : path;
        out += query;
        return out;
    }

    std::string origin() const {
        std::string out = scheme; out += "://"; out += authority();
        return out;
    }
};

// Minimal parser: http[s]://host[:port][/path][?query]
// Also supports path-only strings for relative resolution.
inline Url parse_url(std::string_view s) {
    Url u;

    // scheme
    auto pos = s.find("://");
    if (pos != std::string_view::npos) {
        u.scheme.assign(s.substr(0, pos));
        s.remove_prefix(pos + 3);
    }

    // authority or (relative path)
    if (!u.scheme.empty()) {
        // host[:port][path?query]
        auto slash = s.find('/');
        std::string_view auth = (slash == std::string_view::npos) ? s : s.substr(0, slash);
        if (slash == std::string_view::npos) s = "/";
        else s.remove_prefix(slash);

        auto colon = auth.rfind(':');
        if (colon != std::string_view::npos) {
            u.host.assign(auth.substr(0, colon));
            u.port.assign(auth.substr(colon + 1));
        } else {
            u.host.assign(auth);
        }
    }

    // path + query (or pure relative)
    auto q = s.find('?');
    if (q == std::string_view::npos) {
        u.path.assign(s.empty() ? "/" : std::string(s));
    } else {
        u.path.assign(std::string(s.substr(0, q)));
        u.query.assign(std::string(s.substr(q))); // keep '?'
    }

    // normalize path at least to start with '/'
    if (u.path.empty() || u.path.front() != '/') u.path.insert(u.path.begin(), '/');
    return u;
}

// Resolve a Location header against a base URL (very small subset of RFC 3986)
inline Url resolve_url(const Url& base, std::string_view location) {
    // Absolute URL?
    if (location.find("://") != std::string_view::npos) {
        return parse_url(location);
    }

    // Protocol-relative: //host[:port]/...
    if (location.rfind("//", 0) == 0) {
        Url out = parse_url(std::string(base.scheme) + ":" + std::string(location));
        return out;
    }

    Url out = base;
    // Only an absolute-path or relative-path?
    if (!location.empty() && location.front() == '/') {
        // absolute-path
        out.path.assign(location.begin(), location.end());
        out.query.clear();
        return out;
    }

    // relative-path
    // strip filename from base.path
    auto last_slash = out.path.rfind('/');
    if (last_slash == std::string::npos) out.path = "/";
    else out.path.resize(last_slash + 1);

    out.path.append(location);

    // remove "./" and "../" segments naively
    // (good enough for client follow; avoids full RFC normalization cost)
    for (;;) {
        auto i = out.path.find("/./");
        if (i == std::string::npos) break;
        out.path.erase(i, 2);
    }
    // handle '/../'
    for (;;) {
        auto i = out.path.find("/../");
        if (i == std::string::npos) break;
        auto j = out.path.rfind('/', i - 1);
        if (j == std::string::npos) { out.path.erase(0, i + 3); break; }
        out.path.erase(j, i + 3 - j);
    }

    if (out.path.empty() || out.path.front() != '/') out.path.insert(out.path.begin(), '/');
    out.query.clear();
    return out;
}

} // namespace tb::net
