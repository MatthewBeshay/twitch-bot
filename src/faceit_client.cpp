#include "faceit_client.hpp"
#include "http_client.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include <boost/asio/awaitable.hpp>
#include <boost/json.hpp>


using boost::asio::awaitable;
namespace bj = boost::json;

namespace faceit {
namespace {

// Hosts & port
static constexpr std::string_view v4_host  = "open.faceit.com";
static constexpr std::string_view v1_host  = "api.faceit.com";
static constexpr std::string_view api_port = "443";

// HTTP status -> human-readable reason
static const std::map<int,std::string> kStatusReasons = {
    {200, "OK – Player stats for matches"},
    {401, "Unauthorized – check your API key"},
    {403, "Forbidden – access denied"},
    {404, "Not Found – resource missing"},
    {429, "Too Many Requests – rate limit exceeded"},
    {503, "Temporarily Unavailable – server busy"}
};

static std::string reasonFor(int status)
{
    auto it = kStatusReasons.find(status);
    return it != kStatusReasons.end()
         ? it->second
         : "Generic error";
}

} // namespace

//------------------------------------------------------------------------------
// Constructor
//------------------------------------------------------------------------------
Client::Client(std::string apiKey)
  : apiKey_{std::move(apiKey)}
  , ioContext_{}
  , sslContext_{boost::asio::ssl::context::tlsv12_client}
{
    if (apiKey_.empty())
        throw std::invalid_argument{"FACEIT_API_KEY must be non-empty"};
    sslContext_.set_default_verify_paths();
}

//------------------------------------------------------------------------------
// getPlayerByNickname
//------------------------------------------------------------------------------
awaitable<json>
Client::getPlayerByNickname(std::string_view nickname,
                            std::string_view game)
{
    auto queries = std::vector<std::pair<std::string_view,std::string>>{
        {"nickname", std::string{nickname}},
        {"game",     std::string{game}}
    };
    auto target = buildTarget("/data/v4/players", queries);
    co_return co_await sendRequest(v4_host, std::move(target), true);
}

//------------------------------------------------------------------------------
// getPlayerById
//------------------------------------------------------------------------------
awaitable<json>
Client::getPlayerById(std::string_view playerId)
{
    auto target = std::string{"/data/v4/players/"} + std::string{playerId};
    co_return co_await sendRequest(v4_host, std::move(target), true);
}

//------------------------------------------------------------------------------
// getPlayerStats
//------------------------------------------------------------------------------
awaitable<std::vector<json>>
Client::getPlayerStats(std::string_view        playerId,
                       int64_t                fromTs,
                       std::optional<int64_t> toTs,
                       int                    limit)
{
    std::vector<std::pair<std::string_view,std::string>> q;
    q.reserve(3);
    q.emplace_back("from",  std::to_string(fromTs));
    q.emplace_back("limit", std::to_string(limit));
    if (toTs) q.emplace_back("to", std::to_string(*toTs));

    auto base = std::string{"/data/v4/players/"} + std::string{playerId}
              + "/games/cs2/stats";
    auto target = buildTarget(base, q);

    json res = co_await sendRequest(v4_host, std::move(target), true);
    std::vector<json> out;
    if (res.kind() == bj::kind::object) {
        auto const& obj = res.as_object();
        if (auto it = obj.find("items"); it != obj.end()
         && it->value().kind() == bj::kind::array)
        {
            for (auto& el : it->value().as_array())
                out.push_back(el);
        }
    }
    co_return out;
}

//------------------------------------------------------------------------------
// getEloHistory
//------------------------------------------------------------------------------
awaitable<std::vector<json>>
Client::getEloHistory(std::string_view        playerId,
                      int                     size,
                      int                     page,
                      std::optional<int64_t>  fromTs,
                      std::optional<int64_t>  toTs)
{
    std::vector<std::pair<std::string_view,std::string>> q;
    q.reserve(4);
    q.emplace_back("size", std::to_string(size));
    q.emplace_back("page", std::to_string(page));
    if (fromTs) q.emplace_back("from", std::to_string(*fromTs));
    if (toTs)   q.emplace_back("to",   std::to_string(*toTs));

    auto base = std::string{"/stats/v1/stats/time/users/"} + std::string{playerId}
              + "/games/cs2";
    auto target = buildTarget(base, q);

    json res = co_await sendRequest(v1_host, std::move(target), false);
    std::vector<json> out;
    if (res.kind() == bj::kind::array) {
        for (auto& el : res.as_array())
            out.push_back(el);
    }
    else if (res.kind() == bj::kind::object) {
        auto const& obj = res.as_object();
        if (auto it = obj.find("items"); it != obj.end()
         && it->value().kind() == bj::kind::array)
        {
            for (auto& el : it->value().as_array())
                out.push_back(el);
        }
    }
    else {
        throw std::runtime_error("Unexpected JSON format in elo_history");
    }
    co_return out;
}

//------------------------------------------------------------------------------
// getMatchStats
//------------------------------------------------------------------------------
awaitable<json>
Client::getMatchStats(std::string_view matchId)
{
    auto target = std::string{"/data/v4/matches/"} + std::string{matchId}
                + "/stats";
    co_return co_await sendRequest(v4_host, std::move(target), true);
}

//------------------------------------------------------------------------------
// sendRequest
//------------------------------------------------------------------------------
awaitable<json>
Client::sendRequest(std::string_view host,
                    std::string      target,
                    bool             includeAuth)
{
    // Prepare HTTP headers
    std::map<std::string,std::string> hdrs{
        {"Accept", "application/json"}
    };
    if (includeAuth)
        hdrs.emplace("Authorization", "Bearer " + apiKey_);

    try {
        // May throw std::runtime_error("status=XYZ …")
        co_return co_await http_client::get(
            host, api_port, std::move(target),
            hdrs, ioContext_, sslContext_);
    }
    catch (const std::runtime_error& e) {
        // Extract HTTP status code if present
        std::string what = e.what();
        int code = 0;
        if (auto pos = what.find("status="); pos != std::string::npos)
            code = std::stoi(what.substr(pos + 7));

        std::cerr
            << "[FACEIT][" << host << target << "] "
            << "HTTP " << code << " – " << reasonFor(code)
            << "\n    underlying: " << what << "\n";
        throw;
    }
    catch (const boost::system::system_error& e) {
        std::cerr
            << "[FACEIT][" << host << target << "] "
            << "Network error: " << e.code().message() << "\n";
        throw;
    }
}

//------------------------------------------------------------------------------
// buildTarget
//------------------------------------------------------------------------------
std::string
Client::buildTarget(
    std::string_view base,
    std::vector<std::pair<std::string_view,std::string>> const& queries)
{
    if (queries.empty())
        return std::string{base};

    size_t cap = base.size() + 1;
    for (auto const& [k,v] : queries)
        cap += k.size() + v.size() + 2;

    std::string tgt;
    tgt.reserve(cap);
    tgt.append(base).push_back('?');

    for (size_t i = 0; i < queries.size(); ++i) {
        auto const& [k,v] = queries[i];
        tgt += urlEncode(k);
        tgt.push_back('=');
        tgt += urlEncode(v);
        if (i + 1 < queries.size())
            tgt.push_back('&');
    }
    return tgt;
}

//------------------------------------------------------------------------------
// urlEncode
//------------------------------------------------------------------------------
std::string
Client::urlEncode(std::string_view value) noexcept
{
    std::string out;
    out.reserve(value.size());
    char buf[4];

    for (unsigned char c : value) {
        if (std::isalnum(c) || c=='-' || c=='_' || c=='.' || c=='~') {
            out.push_back(c);
        } else {
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            out.append(buf);
        }
    }
    return out;
}

} // namespace faceit
