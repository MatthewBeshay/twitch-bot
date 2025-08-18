// C++ Standard Library
#include <algorithm>
#include <cctype>

// Project
#include <tb/net/http/mime.hpp>

namespace tb::net::mime {

namespace {
    inline void trim(std::string_view& sv)
    {
        while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
            sv.remove_prefix(1);
        }
        while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) {
            sv.remove_suffix(1);
        }
    }
    inline std::string to_lower(std::string_view sv)
    {
        std::string s;
        s.reserve(sv.size());
        for (unsigned char c : sv)
            s.push_back(static_cast<char>(std::tolower(c)));
        return s;
    }
} // namespace

std::optional<media_type> parse(std::string_view ct, std::error_code& ec)
{
    ec = {};
    trim(ct);
    if (ct.empty()) {
        ec = errc::invalid_content_type;
        return std::nullopt;
    }

    auto slash = ct.find('/');
    if (slash == std::string_view::npos) {
        ec = errc::invalid_content_type;
        return std::nullopt;
    }

    std::string t = to_lower(ct.substr(0, slash));
    std::string_view rest = ct.substr(slash + 1);
    auto semi = rest.find(';');
    std::string st = to_lower(semi == std::string_view::npos ? rest : rest.substr(0, semi));
    std::string cs;

    if (semi != std::string_view::npos) {
        std::string_view params = rest.substr(semi + 1);
        while (!params.empty()) {
            auto next_semi = params.find(';');
            std::string_view kv
                = (next_semi == std::string_view::npos) ? params : params.substr(0, next_semi);
            params = (next_semi == std::string_view::npos) ? std::string_view{}
                                                           : params.substr(next_semi + 1);

            trim(kv);
            if (kv.empty())
                continue;
            auto eq = kv.find('=');
            std::string key = to_lower(eq == std::string_view::npos ? kv : kv.substr(0, eq));
            std::string_view val_sv
                = (eq == std::string_view::npos) ? std::string_view{} : kv.substr(eq + 1);
            trim(val_sv);

            if (key == "charset") {
                if (!val_sv.empty() && (val_sv.front() == '"' || val_sv.front() == '\'')) {
                    char q = val_sv.front();
                    if (val_sv.size() >= 2 && val_sv.back() == q) {
                        val_sv = val_sv.substr(1, val_sv.size() - 2);
                    }
                }
                cs = to_lower(val_sv);
            }
        }
    }

    media_type mt{std::move(t), std::move(st), std::move(cs)};
    return mt;
}

} // namespace tb::net::mime
