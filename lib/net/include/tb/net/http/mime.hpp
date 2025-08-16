#pragma once
#include <string>
#include <string_view>
#include <optional>
#include <tb/net/http/error.hpp>

namespace tb::net::mime {

struct media_type {
    std::string type;    // e.g. "application"
    std::string subtype; // e.g. "json"
    std::string charset; // lowercased if present; empty if absent

    [[nodiscard]] std::string to_string() const {
        std::string out = type;
        out.push_back('/');
        out += subtype;
        if (!charset.empty()) {
            out += "; charset=";
            out += charset;
        }
        return out;
    }

    [[nodiscard]] bool is_json_like() const {
        // application/json, application/*+json
        if (type == "application") {
            if (subtype == "json") return true;
            auto plus = subtype.rfind("+json");
            if (plus != std::string::npos && plus + 5 == subtype.size()) return true;
        }
        return false;
    }
};

// Parse a Content-Type header (case-insensitive keys, tolerant spaces).
// Returns {type, subtype, charset} on success; error on completely invalid input.
[[nodiscard]] std::optional<media_type> parse(std::string_view content_type,
                                              std::error_code& ec);

// Simple helpers
[[nodiscard]] inline bool is_json(std::string_view ct) {
    std::error_code ec;
    if (auto mt = parse(ct, ec)) return mt->is_json_like();
    return false;
}

} // namespace tb::net::mime
