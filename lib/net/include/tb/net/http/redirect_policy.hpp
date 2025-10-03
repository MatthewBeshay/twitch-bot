/*
Module Name:
- redirect_policy.hpp

Abstract:
- Redirect handling policy for an HTTP client.
- Encodes hop limits and rules: follow_none, safe_only, same_origin, follow_all.
- next_verb applies HTTP semantics: 307/308 keep method; 303 becomes GET;
  legacy 301/302 convert POST to GET for web compatibility.
*/
#pragma once

// C++ Standard Library
#include <cstddef>
#include <string_view>

// Boost.Beast
#include <boost/beast/http/verb.hpp>

namespace tb::net
{

    inline constexpr bool is_redirect_status(int s) noexcept
    {
        return s == 301 || s == 302 || s == 303 || s == 307 || s == 308;
    }

    inline constexpr bool keep_method_on_redirect(int s) noexcept
    {
        return s == 307 || s == 308;
    }

    enum class RedirectMode
    {
        follow_none,
        safe_only,
        same_origin,
        follow_all
    };

    class RedirectPolicy
    {
    public:
        explicit RedirectPolicy(std::size_t max_hops = 5,
                                RedirectMode mode = RedirectMode::safe_only) noexcept
            :
            max_hops_(max_hops), mode_(mode)
        {
        }

        std::size_t max_hops() const noexcept
        {
            return max_hops_;
        }
        void set_max_hops(std::size_t n) noexcept
        {
            max_hops_ = n;
        }

        RedirectMode mode() const noexcept
        {
            return mode_;
        }
        void set_mode(RedirectMode m) noexcept
        {
            mode_ = m;
        }

        // Method rewriting per HTTP:
        // - 307/308 preserve the original method.
        // - 303 always becomes GET.
        // - 301/302: convert POST to GET (prevailing browser behaviour).
        static boost::beast::http::verb next_verb(boost::beast::http::verb cur, int status) noexcept
        {
            if (keep_method_on_redirect(status))
            {
                return cur;
            }
            if (status == 303)
            {
                return boost::beast::http::verb::get;
            }
            if (cur == boost::beast::http::verb::post)
            {
                return boost::beast::http::verb::get;
            }

            return cur;
        }

        static bool same_origin(std::string_view scheme_a,
                                std::string_view host_a,
                                std::string_view port_a,
                                std::string_view scheme_b,
                                std::string_view host_b,
                                std::string_view port_b) noexcept
        {
            return scheme_a == scheme_b && host_a == host_b && port_a == port_b;
        }

        // Decide whether to follow a hop from 'from' to 'to' given the resulting method.
        template<class Url>
        bool
        allow_hop(const Url& from, const Url& to, boost::beast::http::verb resulting) const noexcept
        {
            switch (mode_)
            {
            case RedirectMode::follow_none:
                return false;
            case RedirectMode::safe_only:
                return resulting == boost::beast::http::verb::get || resulting == boost::beast::http::verb::head;
            case RedirectMode::same_origin:
                return same_origin(from.scheme, from.host, from.port, to.scheme, to.host, to.port);
            case RedirectMode::follow_all:
                return true;
            }
            return false;
        }

    private:
        std::size_t max_hops_;
        RedirectMode mode_;
    };

} // namespace tb::net
