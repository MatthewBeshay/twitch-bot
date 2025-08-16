#pragma once

#include <cstddef>
#include <string_view>
#include <boost/beast/http/verb.hpp>

namespace tb::net {

// --- RFC 7231 / 9110 redirect helpers ---------------------------------------

inline constexpr bool is_redirect_status(int s) noexcept {
    return s == 301 || s == 302 || s == 303 || s == 307 || s == 308;
}

// 307/308 must keep the original method; 303 switches to GET;
// historic behavior for 301/302: POST becomes GET (others keep method).
inline constexpr bool keep_method_on_redirect(int s) noexcept {
    return s == 307 || s == 308;
}

// --- Policy ------------------------------------------------------------------

enum class RedirectMode {
    follow_none,   // never follow
    safe_only,     // follow only when resulting method is GET/HEAD
    same_origin,   // follow any method but only to same (scheme, host, port)
    follow_all     // follow anything (bounded by max_hops)
};

class RedirectPolicy {
public:
    explicit RedirectPolicy(std::size_t max_hops = 5,
                            RedirectMode mode = RedirectMode::safe_only) noexcept
        : max_hops_(max_hops), mode_(mode) {}

    std::size_t   max_hops() const noexcept { return max_hops_; }
    void          set_max_hops(std::size_t n) noexcept { max_hops_ = n; }

    RedirectMode  mode() const noexcept { return mode_; }
    void          set_mode(RedirectMode m) noexcept { mode_ = m; }

    // Decide next method according to status and current method
    static boost::beast::http::verb next_verb(boost::beast::http::verb cur, int status) noexcept {
        if (keep_method_on_redirect(status)) return cur;
        if (status == 303) return boost::beast::http::verb::get;
        // historic behavior for 301/302: POST -> GET
        if (cur == boost::beast::http::verb::post) return boost::beast::http::verb::get;
        return cur;
    }

    // Simple (scheme, host, port) equality helper
    static bool same_origin(std::string_view scheme_a, std::string_view host_a, std::string_view port_a,
                            std::string_view scheme_b, std::string_view host_b, std::string_view port_b) noexcept {
        return scheme_a == scheme_b && host_a == host_b && port_a == port_b;
    }

    // Allow/deny a hop based on mode and resulting method
    template <class Url>
    bool allow_hop(const Url& from, const Url& to,
                   boost::beast::http::verb resulting) const noexcept {
        switch (mode_) {
        case RedirectMode::follow_none:
            return false;
        case RedirectMode::safe_only:
            return resulting == boost::beast::http::verb::get ||
                   resulting == boost::beast::http::verb::head;
        case RedirectMode::same_origin:
            return same_origin(from.scheme, from.host, from.port,
                               to.scheme,   to.host,   to.port);
        case RedirectMode::follow_all:
            return true;
        }
        return false;
    }

private:
    std::size_t  max_hops_;
    RedirectMode mode_;
};

} // namespace tb::net
