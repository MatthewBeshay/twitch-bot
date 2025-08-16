#pragma once

// C++ Standard Library
#include <cctype>
#include <string>
#include <string_view>
#include <system_error>

// Project
#include <tb/net/http/error.hpp>

namespace tb::net::encoding {

// bitmask of supported encodings
enum class enc : unsigned {
    none = 0,
    gzip = 1u << 0,
    br = 1u << 1,
    deflate = 1u << 2, // reserved (not implemented)
};

constexpr enc operator|(enc a, enc b)
{
    return static_cast<enc>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
constexpr enc operator&(enc a, enc b)
{
    return static_cast<enc>(static_cast<unsigned>(a) & static_cast<unsigned>(b));
}
constexpr bool any(enc v)
{
    return static_cast<unsigned>(v) != 0;
}

// Parse a Content-Encoding header like: "gzip, br"
[[nodiscard]] enc parse_content_encoding(std::string_view value);

// Individual backends (implemented in gzip_decoder.cpp and br_decoder.cpp)
[[nodiscard]] bool gzip_decode(std::string_view in, std::string& out, std::error_code& ec);
[[nodiscard]] bool br_decode(std::string_view in, std::string& out, std::error_code& ec);

// Decode according to a single encoding (identity/gzip/br)
[[nodiscard]] inline bool
decode(std::string_view in, enc which, std::string& out, std::error_code& ec)
{
    if (which == enc::none) {
        out.assign(in.begin(), in.end());
        ec = {};
        return true;
    }
    if ((which & enc::gzip) == enc::gzip) {
        return gzip_decode(in, out, ec);
    }
    if ((which & enc::br) == enc::br) {
        return br_decode(in, out, ec);
    }
    ec = errc::unsupported_encoding;
    return false;
}

static inline void trim(std::string_view& sv)
{
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())))
        sv.remove_prefix(1);
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())))
        sv.remove_suffix(1);
}
static inline std::string lower(std::string_view sv)
{
    std::string out;
    out.reserve(sv.size());
    for (unsigned char c : sv)
        out.push_back(static_cast<char>(std::tolower(c)));
    return out;
}

inline enc parse_content_encoding(std::string_view value)
{
    enc result = enc::none;
    while (!value.empty()) {
        const auto comma = value.find(',');
        std::string_view token = (comma == std::string_view::npos) ? value : value.substr(0, comma);
        value = (comma == std::string_view::npos) ? std::string_view{} : value.substr(comma + 1);
        trim(token);
        if (token.empty())
            continue;
        const std::string t = lower(token);
        if (t == "identity") { /* no-op */
        } else if (t == "gzip")
            result = result | enc::gzip;
        else if (t == "x-gzip")
            result = result | enc::gzip;
        else if (t == "br")
            result = result | enc::br;
        else if (t == "deflate")
            result = result | enc::deflate;
        else { /* unknown -> ignore */
        }
    }
    return result;
}

} // namespace tb::net::encoding
