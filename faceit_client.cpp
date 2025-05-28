// faceit_client.cpp
#include "faceit_client.hpp"
#include "http_client.hpp"         // our generic sync GET over HTTPS
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core/flat_buffer.hpp>
#include <stdexcept>

using json = nlohmann::json;

namespace faceit {

    namespace {
        // Hosts & defaults
        static constexpr std::string_view v4_host = "open.faceit.com";
        static constexpr std::string_view v1_host = "api.faceit.com";
        static constexpr std::string_view api_port = "443";
    }

    // ——— Constructor ———————————————————————————————————————————————
    Client::Client(std::string api_key)
        : api_key_{ std::move(api_key) },
        ssl_ctx_{ boost::asio::ssl::context::tlsv12_client }
    {
        if (api_key_.empty())
            throw std::invalid_argument{ "FACEIT_API_KEY must be non-empty" };
        ssl_ctx_.set_default_verify_paths();
    }

    // ——— Public API ———————————————————————————————————————————————

    json Client::get_player_by_nickname(std::string_view nickname,
        std::string_view game)
    {
        std::vector<std::pair<std::string_view, std::string>> q{
            {"nickname", std::string{nickname}},
            {"game",     std::string{game}}
        };
        auto target = build_target("/data/v4/players", q);
        return send_request(v4_host, std::move(target), /*auth=*/true);
    }

    json Client::get_player_by_id(std::string_view player_id)
    {
        auto target = std::string{ "/data/v4/players/" } + std::string{ player_id };
        return send_request(v4_host, std::move(target), /*auth=*/true);
    }

    std::vector<json>
        Client::get_player_stats(std::string_view        player_id,
            int64_t                from_ts,
            std::optional<int64_t> to_ts,
            int                    limit)
    {
        std::vector<std::pair<std::string_view, std::string>> q;
        q.reserve(3);
        q.emplace_back("from", std::to_string(from_ts));
        q.emplace_back("limit", std::to_string(limit));
        if (to_ts) q.emplace_back("to", std::to_string(*to_ts));

        auto base = std::string{ "/data/v4/players/" }
            + std::string{ player_id }
        + "/games/cs2/stats";
        auto target = build_target(base, q);

        auto arr = send_request(v4_host, std::move(target), /*auth=*/true);
        std::vector<json> out;
        if (auto it = arr.find("items"); it != arr.end() && it->is_array()) {
            out.reserve(it->size());
            for (auto& el : *it) out.push_back(std::move(el));
        }
        return out;
    }

    std::vector<json>
        Client::get_elo_history(std::string_view        player_id,
            int                     size,
            int                     page,
            std::optional<int64_t>  from_ts,
            std::optional<int64_t>  to_ts)
    {
        std::vector<std::pair<std::string_view, std::string>> q;
        q.reserve(4);
        q.emplace_back("size", std::to_string(size));
        q.emplace_back("page", std::to_string(page));
        if (from_ts) q.emplace_back("from", std::to_string(*from_ts));
        if (to_ts)   q.emplace_back("to", std::to_string(*to_ts));

        auto base = std::string{ "/stats/v1/stats/time/users/" }
            + std::string{ player_id }
        + "/games/cs2";
        auto target = build_target(base, q);

        auto data = send_request(v1_host, std::move(target), /*auth=*/false);
        std::vector<json> out;
        if (data.is_array()) {
            out.reserve(data.size());
            for (auto& el : data) out.push_back(std::move(el));
        }
        else if (auto it = data.find("items"); it != data.end() && it->is_array()) {
            out.reserve(it->size());
            for (auto& el : *it) out.push_back(std::move(el));
        }
        else {
            throw std::runtime_error("Unexpected JSON format in elo_history");
        }
        return out;
    }

    json Client::get_match_stats(std::string_view match_id)
    {
        auto target = std::string{ "/data/v4/matches/" }
            + std::string{ match_id }
        + "/stats";
        return send_request(v4_host, std::move(target), /*auth=*/true);
    }

    // ——— Helpers ——————————————————————————————————————————————————

    json Client::send_request(std::string_view host,
        std::string      target,
        bool             include_auth)
    {
        // Prepare headers
        std::map<std::string, std::string> hdrs{
            {"Accept", "application/json"}
        };
        if (include_auth) {
            hdrs.emplace("Authorization", "Bearer " + api_key_);
        }

        // Perform synchronous GET → returns body string
        auto body = http_client::get(host, api_port, target, hdrs, ioc_, ssl_ctx_);

        // Parse JSON and return
        return json::parse(body);
    }

    std::string Client::build_target(
        std::string_view base,
        std::vector<std::pair<std::string_view, std::string>> const& queries)
    {
        if (queries.empty())
            return std::string{ base };

        // reserve enough for “base?key=val&…” 
        size_t cap = base.size() + 1;
        for (auto const& [k, v] : queries) cap += k.size() + v.size() + 2;
        std::string tgt;
        tgt.reserve(cap);

        tgt.append(base).push_back('?');
        for (size_t i = 0; i < queries.size(); ++i) {
            auto const& [k, v] = queries[i];
            tgt += url_encode(k);
            tgt.push_back('=');
            tgt += url_encode(v);
            if (i + 1 < queries.size()) tgt.push_back('&');
        }
        return tgt;
    }

    std::string Client::url_encode(std::string_view value) noexcept
    {
        std::string out;
        out.reserve(value.size());
        char buf[4];
        for (unsigned char c : value) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                out.push_back(c);
            }
            else {
                std::snprintf(buf, sizeof(buf), "%%%02X", c);
                out.append(buf);
            }
        }
        return out;
    }

} // namespace faceit
