#include "twitch_bot.hpp"
#include "http_client.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/system/error_code.hpp>
#include <boost/json.hpp>



using asio::awaitable;
using asio::use_awaitable;
namespace bj = boost::json;

// Helper to convert tm in UTC to time_t
static time_t portable_timegm(std::tm* tm) {
#if defined(_WIN32) || defined(_WIN64)
    return _mkgmtime(tm);
#else
    return timegm(tm);
#endif
}

static constexpr char const* helix_host = "api.twitch.tv";
static constexpr char const* helix_port = "443";

// ─── Constructor & Teardown ─────────────────────────────────────────────────

TwitchBot::TwitchBot(std::string oauthToken,
                     std::string clientId,
                     std::string clientSecret,
                     std::string controlChannel,
                     std::string faceitApiKey)
  : ioc_{}
  , sslCtx_{asio::ssl::context::tlsv12_client}
  , socket_{ioc_, sslCtx_}
  , pingTimer_{ioc_}
  , oauthToken_{std::move(oauthToken)}
  , clientId_{std::move(clientId)}
  , clientSecret_{std::move(clientSecret)}
  , controlChannel_{std::move(controlChannel)}
  , faceitClient_{std::move(faceitApiKey)}
{
    sslCtx_.set_default_verify_paths();
    loadChannels();
}

TwitchBot::~TwitchBot() noexcept
{
    boost::system::error_code ec;
    socket_.close(ec);
    pingTimer_.cancel();
    ioc_.stop();
}

// ─── Public API ──────────────────────────────────────────────────────────────

void TwitchBot::run()
{
    // Launch top-level coroutine
    boost::asio::co_spawn(
        ioc_,
        [this]() -> awaitable<void> { co_await runBot(); },
        boost::asio::detached);

    ioc_.run();
}

void TwitchBot::addChatListener(ChatListener cb)
{
    chatListeners_.push_back(std::move(cb));
}

// ─── Top-level coroutine ─────────────────────────────────────────────────────

awaitable<void> TwitchBot::runBot()
{
    // 1) Connect
    try {
        co_await connectWebSocket();
    }
    catch (std::exception const& e) {
        std::cerr << "[TwitchBot] connect error: " << e.what() << "\n";
        co_return;
    }

    // 2) Spawn ping + read loops
    auto ex = co_await asio::this_coro::executor;
    boost::asio::co_spawn(ex, schedulePing(), boost::asio::detached);
    boost::asio::co_spawn(ex, readLoop(),      boost::asio::detached);

    // 3) Keep alive indefinitely
    asio::steady_timer idle{ex};
    idle.expires_at(std::chrono::steady_clock::time_point::max());
    co_await idle.async_wait(use_awaitable);
}

// ─── Connection & Ping ───────────────────────────────────────────────────────

awaitable<void> TwitchBot::connectWebSocket()
{
    constexpr char const* host = "irc-ws.chat.twitch.tv";
    constexpr char const* port = "443";

    socket_.connect(host, port);

    co_await sendRaw("PASS " + oauthToken_);
    co_await sendRaw("NICK " + controlChannel_);
    co_await sendRaw("CAP REQ :twitch.tv/membership twitch.tv/tags twitch.tv/commands");

    co_await joinChannel(controlChannel_);
    for (auto const& [ch, _] : channelNicks_)
        co_await joinChannel(ch);
}

awaitable<void> TwitchBot::schedulePing()
{
    for (;;) {
        pingTimer_.expires_after(std::chrono::minutes(4));
        co_await pingTimer_.async_wait(use_awaitable);
        co_await sendRaw("PING :tmi.twitch.tv");
    }
}

// ─── Read Loop & Dispatch ─────────────────────────────────────────────────────

awaitable<void> TwitchBot::readLoop()
{
    try {
        for (;;) {
            auto line = co_await socket_.readLineAsync();
            if (!line.empty())
                co_await onMessage(line);
        }
    }
    catch (boost::system::system_error const& e) {
        std::cerr << "[TwitchBot] readLoop error: " << e.what() << "\n";
    }
}

// ─── IRC Framing & Writes ─────────────────────────────────────────────────────

awaitable<void> TwitchBot::sendRaw(std::string_view msg) noexcept
{
    try {
        co_await socket_.writeLineAsync(msg);
    }
    catch (std::exception const& e) {
        std::cerr << "[TwitchBot] sendRaw error: " << e.what() << "\n";
    }
}

awaitable<void> TwitchBot::joinChannel(std::string_view channel) noexcept
{
    co_await sendRaw("JOIN #" + std::string(channel));
}

awaitable<void> TwitchBot::leaveChannel(std::string_view channel) noexcept
{
    co_await sendRaw("PART #" + std::string(channel));
}

// ─── Parsing & Dispatch ──────────────────────────────────────────────────────

IrcMessage TwitchBot::parseIrc(std::string_view line)
{
    IrcMessage msg;
    size_t pos = 0;

    // 1) Tags
    if (pos < line.size() && line[pos] == '@') {
        auto end = line.find(' ', pos);
        auto list = line.substr(1, end - 1);
        size_t p = 0;
        while (p < list.size()) {
            auto q  = list.find(';', p);
            auto kv = list.substr(p, (q == p ? 0 : q - p));
            if (auto eq = kv.find('='); eq != kv.npos)
                msg.tags[kv.substr(0, eq)] = kv.substr(eq + 1);
            p = (q == kv.npos ? list.size() : q + 1);
        }
        pos = end + 1;
    }

    // 2) Prefix
    if (pos < line.size() && line[pos] == ':') {
        auto end = line.find(' ', pos);
        msg.prefix = line.substr(pos, end - pos);
        pos = end + 1;
    }

    // 3) Command
    {
        auto end = line.find(' ', pos);
        msg.command = line.substr(pos, end - pos);
        pos = end + 1;
    }

    // 4) Params + trailing
    while (pos < line.size()) {
        if (line[pos] == ':') {
            msg.trailing = line.substr(pos + 1);
            break;
        }
        auto end = line.find(' ', pos);
        msg.params.push_back(line.substr(pos, end - pos));
        pos = (end == line.npos ? line.size() : end + 1);
    }

    return msg;
}

awaitable<void> TwitchBot::onMessage(std::string_view raw)
{
    std::cout << "[IRC] " << raw << "\n";
    size_t pos = 0;

    while (pos < raw.size()) {
        auto next = raw.find("\r\n", pos);
        auto line = raw.substr(
            pos,
            next == raw.npos ? raw.size() - pos : next - pos);
        pos = (next == raw.npos ? raw.size() : next + 2);

        if (line.empty()) continue;
        auto m = parseIrc(line);

        if (m.command == "PING") {
            co_await sendRaw("PONG " + std::string(m.trailing));
        }
        else if (m.command == "RECONNECT") {
            std::cerr << "[DEBUG] RECONNECT, reconnecting…\n";
            {
                boost::system::error_code ec;
                socket_.close(ec);
            }
            co_await connectWebSocket();
        }
        else if (m.command == "PRIVMSG" && !m.params.empty()) {
            auto rawChan = m.params[0];
            std::string_view channel = (rawChan.front() == '#')
                ? rawChan.substr(1) : rawChan;
            co_await onPrivMsg(channel, m);
        }
        else if (m.command == "CLEARCHAT" && !m.params.empty()) {
            std::cout << "[CLEARCHAT] channel=" << m.params[0]
                      << " target=" << (m.trailing.empty() ? "<all>" : m.trailing)
                      << "\n";
        }
    }

    co_return;
}

awaitable<void> TwitchBot::onPrivMsg(std::string_view channel,
                                     IrcMessage const& m)
{
    auto excl = m.prefix.find('!');
    std::string_view user = m.prefix.substr(1, excl - 1);
    std::string_view text = m.trailing;

    if (!text.empty() && text.front() == '!')
        co_await handleCommand(channel, user, text);

    for (auto& cb : chatListeners_)
        cb(channel, user, text);

    co_return;
}

awaitable<void> TwitchBot::handleCommand(std::string_view channel,
                                         std::string_view user,
                                         std::string_view content)
{
    auto space = content.find(' ');
    auto cmd   = content.substr(0, space);
    auto args  = (space == content.npos)
               ? std::string_view{}
               : content.substr(space + 1);

    if (cmd == "!join")       co_await cmdJoin(user);
    else if (cmd == "!leave") co_await cmdLeave(user);
    else if (cmd == "!setnickname" && channel == controlChannel_)
                                co_await cmdSetNickname(channel, args);
    else if (cmd == "!rank")  co_await cmdRank(channel);
    else if (cmd == "!record" || cmd == "!recordcs") {
        int limit = 100;
        if (!args.empty()) {
            try { limit = std::clamp(std::stoi(std::string(args)), 1, 100); }
            catch(...) {}
        }
        co_await cmdRecord(channel, limit);
    }

    co_return;
}

// ─── Command Handlers ────────────────────────────────────────────────────────

awaitable<void> TwitchBot::cmdJoin(std::string_view user) noexcept
{
    co_await joinChannel(user);
    channelNicks_[std::string(user)] = std::nullopt;
    saveChannels();
    co_await sendRaw("PRIVMSG #" + controlChannel_ + " :Joined " + std::string(user));
    co_return;
}

awaitable<void> TwitchBot::cmdLeave(std::string_view user) noexcept
{
    co_await leaveChannel(user);
    channelNicks_.erase(std::string(user));
    saveChannels();
    co_await sendRaw("PRIVMSG #" + controlChannel_ + " :Left " + std::string(user));
    co_return;
}

awaitable<void> TwitchBot::cmdSetNickname(std::string_view channel,
                                          std::string_view nickname) noexcept
{
    channelNicks_[std::string(channel)] = std::string(nickname);
    saveChannels();
    co_await sendRaw("PRIVMSG #" + std::string(channel)
                   + " :FACEIT nickname set to " + std::string(nickname));
    co_return;
}

awaitable<void> TwitchBot::cmdRecord(std::string_view channel, int limit)
{
    auto startOpt = co_await getStreamStart(channel);
    if (!startOpt) {
        co_await sendRaw("PRIVMSG #" + std::string(channel)
                       + " :Stream is offline");
        co_return;
    }

    auto it = channelNicks_.find(std::string(channel));
    if (it == channelNicks_.end() || !it->second) {
        co_await sendRaw("PRIVMSG #" + std::string(channel)
                       + " :Use !setnickname first");
        co_return;
    }

    try {
        bj::value player = co_await faceitClient_
            .getPlayerByNickname(*it->second, "cs2");
        auto& obj = player.as_object();
        std::string pid = std::string(obj.at("player_id").as_string());
        int currentElo = obj.at("games").as_object()
                            .at("cs2").as_object()
                            .at("faceit_elo").to_number<int>();

        int64_t fromMs = startOpt->count();
        auto stats = co_await faceitClient_
            .getPlayerStats(pid, fromMs, std::nullopt, limit);

        int wins = std::count_if(stats.begin(), stats.end(),
            [](auto const& m){
                return m.as_object()
                        .at("stats").as_object()
                        .at("Result").as_string() == "1";
            });
        int losses = int(stats.size()) - wins;

        auto history = co_await faceitClient_
            .getEloHistory(pid, limit, 0, fromMs, std::nullopt);
        std::sort(history.begin(), history.end(),
            [](auto const& a, auto const& b){
                return a.as_object()
                        .at("date").to_number<int64_t>()
                     <  b.as_object()
                        .at("date").to_number<int64_t>();
            });

        int eloChange = 0;
        if (history.size() >= 2) {
            auto& f = history.front().as_object();
            auto& l = history.back().as_object();
            int first = std::stoi(std::string(f.at("elo").as_string()));
            int last  = std::stoi(std::string(l.at("elo").as_string()));
            eloChange = last - first;
        }

        std::ostringstream oss;
        oss << "PRIVMSG #" << channel << " :"
            << wins << "W/" << losses << "L ("
            << stats.size() << ") | Elo " << currentElo
            << (eloChange >= 0 ? " (+" : " (") << eloChange << ")";
        co_await sendRaw(oss.str());
    }
    catch (std::exception const& e) {
        std::cerr << "[cmdRecord] error: " << e.what() << "\n";
    }

    co_return;
}

awaitable<void> TwitchBot::cmdRank(std::string_view channelSv)
{
    std::string channel{channelSv};
    auto it = channelNicks_.find(channel);
    if (it == channelNicks_.end() || !it->second) {
        co_await sendRaw("PRIVMSG #"+channel
                       + " :Set nickname first");
        co_return;
    }

    try {
        bj::value player = co_await faceitClient_
            .getPlayerByNickname(*it->second, "cs2");
        auto& so = player.as_object()
                    .at("games").as_object()
                    .at("cs2").as_object();
        int elo = so.at("faceit_elo").to_number<int>();

        struct LevelInfo { int level, minElo, maxElo; };
        static constexpr LevelInfo levels[] = {
            {10,2001,INT_MAX},{9,1751,2000},{8,1531,1750},
            {7,1351,1530},{6,1201,1350},{5,1051,1200},
            {4, 901,1050},{3, 751, 900},{2, 501, 750},{1, 100, 500}
        };
        int lvl = 1;
        for (auto const& info : levels) {
            if (elo >= info.minElo && elo <= info.maxElo) {
                lvl = info.level;
                break;
            }
        }

        co_await sendRaw("PRIVMSG #"+channel
                       + " :Level " + std::to_string(lvl)
                       + " : " + std::to_string(elo) + " Elo");
    }
    catch (std::exception const& e) {
        std::cerr << "[cmdRank] error: " << e.what() << "\n";
    }

    co_return;
}

// ─── Helix Token & Stream Lookup ─────────────────────────────────────────────

awaitable<void> TwitchBot::ensureHelixToken()
{
    using namespace std::chrono;
    auto now = steady_clock::now();
    if (!helixToken_.empty() && now < helixExpiry_)
        co_return;

    try {
        std::string body = "client_id="   + clientId_
                         + "&client_secret=" + clientSecret_
                         + "&grant_type=client_credentials";
        bj::value j = co_await http_client::post(
            "id.twitch.tv", helix_port, "/oauth2/token",
            body,
            {{"Content-Type","application/x-www-form-urlencoded"}},
            ioc_, sslCtx_);

        auto& obj = j.as_object();
        helixToken_ = std::string(obj.at("access_token").as_string());
        helixExpiry_ = now + seconds{obj.at("expires_in").to_number<int>()};
    }
    catch (...) {
        helixToken_.clear();
    }
    co_return;
}

awaitable<std::optional<std::chrono::milliseconds>>
TwitchBot::getStreamStart(std::string_view channel)
{
    co_await ensureHelixToken();
    if (helixToken_.empty())
        co_return std::nullopt;

    try {
        std::string target = "/helix/streams?user_login=" + std::string(channel);
        bj::value j = co_await http_client::get(
            helix_host, helix_port, target,
            {{"Client-ID", clientId_},
             {"Authorization", "Bearer " + helixToken_}},
            ioc_, sslCtx_);

        auto& data = j.as_object().at("data");
        if (data.kind() != bj::kind::array || data.as_array().empty())
            co_return std::nullopt;

        std::string ts = std::string(data.as_array()[0]
                              .as_object().at("started_at").as_string());
        std::tm tm{};
        std::istringstream ss(ts);
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        if (ss.fail())
            co_return std::nullopt;

        auto sec = portable_timegm(&tm);
        co_return std::chrono::milliseconds{sec * 1000};
    }
    catch (...) {
        co_return std::nullopt;
    }
}

// ─── Persistence ─────────────────────────────────────────────────────────────

void TwitchBot::loadChannels()
{
    std::ifstream in("channels.json");
    if (!in) return;

    std::string content{std::istreambuf_iterator<char>(in), {}};
    bj::value jv = bj::parse(content);

    for (auto const& kv : jv.as_object()) {
        auto const& key = kv.key();
        auto const& val = kv.value();
        if (val.kind() == bj::kind::null)
            channelNicks_[std::string(key)] = std::nullopt;
        else if (val.kind() == bj::kind::string)
            channelNicks_[std::string(key)] = std::string(val.as_string());
    }
}

void TwitchBot::saveChannels()
{
    bj::object obj;
    for (auto const& [chan, opt] : channelNicks_) {
        bj::value v = opt
            ? bj::value(*opt)
            : bj::value(nullptr);
        obj[chan] = std::move(v);
    }
    std::ofstream out("channels.json");
    out << bj::serialize(obj);
}
