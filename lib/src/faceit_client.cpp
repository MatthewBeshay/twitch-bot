// C++ Standard Library
#include <map>
#include <string>
#include <vector>

// 3rd-party
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/system_error.hpp>

// Project
#include "faceit_client.hpp"

namespace faceit {
namespace {

    constexpr std::string_view V4_HOST{"open.faceit.com"};
    constexpr std::string_view V1_HOST{"api.faceit.com"};
    constexpr std::string_view API_PORT{"443"};

} // namespace

Client::Client(boost::asio::any_io_executor executor,
               boost::asio::ssl::context& ssl_ctx,
               std::string apiKey)
    : api_key_{std::move(apiKey)}, http_{executor, ssl_ctx}
{
    if (api_key_.empty()) {
        throw std::invalid_argument{"FACEIT_API_KEY must be non-empty"};
    }
    ssl_ctx.set_default_verify_paths();
}

boost::asio::awaitable<json> Client::get_player_by_nickname(std::string_view nickname,
                                                            std::string_view game)
{
    std::vector<std::pair<std::string_view, std::string>> qs;
    qs.emplace_back("nickname", std::string{nickname});
    qs.emplace_back("game", std::string{game});

    auto target = build_target("/data/v4/players", qs);
    co_return co_await send_v4_request(V4_HOST, std::move(target));
}

boost::asio::awaitable<json> Client::get_player_by_id(std::string_view playerId)
{
    std::string target;
    target.reserve(32 + playerId.size());
    target.assign("/data/v4/players/").append(playerId);
    co_return co_await send_v4_request(V4_HOST, std::move(target));
}

boost::asio::awaitable<std::vector<json>> Client::get_player_stats(std::string_view playerId,
                                                                   int64_t fromTs,
                                                                   std::optional<int64_t> toTs,
                                                                   int limit)
{
    std::vector<std::pair<std::string_view, std::string>> qs;
    qs.reserve(toTs ? 3 : 2);
    qs.emplace_back("from", std::to_string(fromTs));
    qs.emplace_back("limit", std::to_string(limit));
    if (toTs) {
        qs.emplace_back("to", std::to_string(*toTs));
    }

    std::string base;
    base.reserve(64 + playerId.size());
    base.assign("/data/v4/players/").append(playerId).append("/games/cs2/stats");

    auto target = build_target(base, qs);
    json resp = co_await send_v4_request(V4_HOST, std::move(target));

    std::vector<json> out;
    if (resp.is_object()) {
        auto it = resp.get_object().find("items");
        if (it != resp.get_object().end() && it->second.is_array()) {
            for (auto& item : it->second.get_array()) {
                out.push_back(std::move(item));
            }
        }
    }
    co_return out;
}

boost::asio::awaitable<std::vector<json>> Client::get_elo_history(std::string_view playerId,
                                                                  int size,
                                                                  int page,
                                                                  std::optional<int64_t> fromMs,
                                                                  std::optional<int64_t> toMs)
{
    std::vector<std::pair<std::string_view, std::string>> qs;
    qs.reserve(2 + (fromMs ? 1 : 0) + (toMs ? 1 : 0));
    qs.emplace_back("size", std::to_string(size));
    qs.emplace_back("page", std::to_string(page));
    if (fromMs)
        qs.emplace_back("from", std::to_string(*fromMs));
    if (toMs)
        qs.emplace_back("to", std::to_string(*toMs));

    std::string base;
    base.reserve(64 + playerId.size());
    base.assign("/stats/v1/stats/time/users/").append(playerId).append("/games/cs2");

    auto target = build_target(base, qs);
    json resp = co_await send_v1_request(V1_HOST, std::move(target));

    std::vector<json> out;
    if (resp.is_array()) {
        for (auto& item : resp.get_array()) {
            out.push_back(std::move(item));
        }
    } else {
        auto it = resp.get_object().find("items");
        if (it != resp.get_object().end() && it->second.is_array()) {
            for (auto& item : it->second.get_array()) {
                out.push_back(std::move(item));
            }
        }
    }
    co_return out;
}

boost::asio::awaitable<json> Client::get_match_stats(std::string_view matchId)
{
    std::string target;
    target.reserve(32 + matchId.size());
    target.assign("/data/v4/matches/").append(matchId).append("/stats");

    co_return co_await send_v4_request(V4_HOST, std::move(target));
}

boost::asio::awaitable<json> Client::get_match_details(std::string_view matchId)
{
    std::string target;
    target.reserve(32 + matchId.size());
    target.assign("/data/v4/matches/").append(matchId);

    co_return co_await send_v4_request(V4_HOST, std::move(target));
}

boost::asio::awaitable<json> Client::send_v4_request(std::string_view host, std::string target)
{
    std::string auth = "Bearer " + api_key_;
    std::vector<http_client::http_header> hdrs_vec{{"Accept", "application/json"},
                                                   {"Authorization", auth}};
    http_client::http_headers hdrs{hdrs_vec.data(), hdrs_vec.size()};

    auto result = co_await http_.get(host, API_PORT, std::move(target), hdrs);
    if (!result) {
        throw std::runtime_error{"FACEIT v4 API returned error"};
    }
    co_return std::move(*result);
}

boost::asio::awaitable<json> Client::send_v1_request(std::string_view host, std::string target)
{
    // **no** headers for Stats API
    http_client::http_headers hdrs{};
    auto result = co_await http_.get(host, API_PORT, std::move(target), hdrs);
    if (!result) {
        throw std::runtime_error{"FACEIT v1 API returned error"};
    }
    co_return std::move(*result);
}

std::string Client::build_target(std::string_view base,
                                 const std::vector<std::pair<std::string_view, std::string>>& qs)
{
    if (qs.empty()) {
        return std::string{base};
    }

    size_t cap = base.size() + 1;
    for (auto& [k, v] : qs) {
        cap += k.size() + v.size() + 2; // '=' and '&'
    }

    std::string out;
    out.reserve(cap);
    out.assign(base).push_back('?');

    for (size_t i = 0; i < qs.size(); ++i) {
        const auto& [k, v] = qs[i];
        out += url_encode(k);
        out.push_back('=');
        out += url_encode(v);
        if (i + 1 < qs.size()) {
            out.push_back('&');
        }
    }
    return out;
}

std::string Client::url_encode(std::string_view value) noexcept
{
    std::string out;
    out.reserve(value.size());
    char buf[4];

    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(c);
        } else {
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out.append(buf);
        }
    }
    return out;
}

} // namespace faceit
