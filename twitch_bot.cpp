// twitch_bot.cpp
#include "twitch_bot.hpp"
#include "http_client.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <algorithm>

using json = nlohmann::json;

namespace {
    // Portable tm→time_t for UTC
    static time_t portable_timegm(std::tm* tm) {
    #if defined(_WIN32) || defined(_WIN64)
        return _mkgmtime(tm);
    #else
        return timegm(tm);
    #endif
    }

    static constexpr char const* helix_host = "api.twitch.tv";
    static constexpr char const* helix_port = "443";
}

// ——— Construction & Teardown ——————————————————————————————————

TwitchBot::TwitchBot(std::string oauthToken,
                     std::string clientId,
                     std::string clientSecret,
                     std::string controlChannel,
                     std::string faceitApiKey)
  : sslCtx_{asio::ssl::context::tlsv12_client},
    socket_{ioc_, sslCtx_},
    pingTimer_{ioc_},
    oauthToken_{std::move(oauthToken)},
    clientId_{std::move(clientId)},
    clientSecret_{std::move(clientSecret)},
    controlChannel_{std::move(controlChannel)},
    faceitClient_{std::move(faceitApiKey)}
{
    sslCtx_.set_default_verify_paths();
    loadChannels();
}

TwitchBot::~TwitchBot() noexcept {
    boost::system::error_code ec;
    socket_.close(ec);
    ioc_.stop();
    if (readerThread_.joinable())
        readerThread_.join();
}

// ——— Public API ———————————————————————————————————————————————

void TwitchBot::run() {
    connectWebSocket();
    schedulePing();
    readerThread_ = std::thread([this]{ readLoop(); });
    ioc_.run();
}

void TwitchBot::addChatListener(ChatListener cb) {
    chatListeners_.push_back(std::move(cb));
}

// ——— Connection & Ping ————————————————————————————————————————

void TwitchBot::connectWebSocket() {
    constexpr char const* host = "irc-ws.chat.twitch.tv";
    constexpr char const* port = "443";

    socket_.connect(host, port);
    sendRaw("PASS " + oauthToken_);
    sendRaw("NICK " + controlChannel_);
    sendRaw("CAP REQ :twitch.tv/membership twitch.tv/tags twitch.tv/commands");

    joinChannel(controlChannel_);
    for (auto const& [ch,_] : channelNicks_)
        joinChannel(ch);
}

void TwitchBot::schedulePing() {
    pingTimer_.expires_after(std::chrono::minutes(4));
    pingTimer_.async_wait([this](auto ec){
        if (!ec) {
            sendRaw("PING :tmi.twitch.tv");
            schedulePing();
        }
    });
}

// ——— Read Loop & Framing ——————————————————————————————————————

void TwitchBot::readLoop() {
    while (true) {
        boost::system::error_code ec;
        auto line = socket_.readLine(ec);
        if (ec || line.empty()) break;
        onMessage(line);
    }
}

void TwitchBot::sendRaw(std::string_view msg) noexcept {
    boost::system::error_code ec;
    socket_.writeLine(msg, ec);
    if (ec) {
        std::cerr << "[TwitchBot] sendRaw error: "
                  << ec.message() << "\n";
    }
}

void TwitchBot::joinChannel(std::string_view channel) noexcept {
    sendRaw("JOIN #" + std::string(channel));
}

void TwitchBot::leaveChannel(std::string_view channel) noexcept {
    sendRaw("PART #" + std::string(channel));
}

// ——— Message Parsing & Dispatch ——————————————————————————————————

// twitch_bot.cpp (excerpt)

#include <string_view>
#include <unordered_map>

// Parse one raw line into IrcMessage:
static IrcMessage parseIrc(std::string_view line) {
    IrcMessage msg;
    size_t pos = 0;

    // 1) tags?
    if (pos < line.size() && line[pos] == '@') {
        auto end = line.find(' ', pos);
        auto list = line.substr(1, end - 1);
        size_t p = 0, q;
        while (p < list.size()) {
            q = list.find(';', p);
            auto kv = list.substr(p, (q == p ? 0 : q - p));
            auto eq = kv.find('=');
            if (eq != std::string_view::npos) {
                msg.tags[kv.substr(0, eq)] = kv.substr(eq + 1);
            }
            p = (q == std::string_view::npos ? list.size() : q + 1);
        }
        pos = end + 1;
    }

    // 2) prefix?
    if (pos < line.size() && line[pos] == ':') {
        auto end = line.find(' ', pos);
        msg.prefix = line.substr(pos, end - pos);
        pos = end + 1;
    }

    // 3) command
    {
        auto end = line.find(' ', pos);
        msg.command = line.substr(pos, end - pos);
        pos = end + 1;
    }

    // 4) params + trailing
    while (pos < line.size()) {
        if (line[pos] == ':') {
            msg.trailing = line.substr(pos + 1);
            break;
        }
        auto end = line.find(' ', pos);
        msg.params.push_back(line.substr(pos, end - pos));
        if (end == std::string_view::npos) { pos = line.size(); }
        else { pos = end + 1; }
    }

    return msg;
}

void TwitchBot::onMessage(std::string_view rawFrame) {
    size_t pos = 0;
    std::cout << "[IRC] " << rawFrame << "\n";
    while (pos < rawFrame.size()) {
        // 1) Pull out one line (up to CRLF)
        auto next = rawFrame.find("\r\n", pos);
        std::string_view line = rawFrame.substr(
            pos,
            next == std::string_view::npos
            ? rawFrame.size() - pos
            : next - pos
        );
        pos = (next == std::string_view::npos ? rawFrame.size() : next + 2);

        if (line.empty())
            continue;

        // 2) Parse tags/prefix/command/params/trailing
        auto m = parseIrc(line);

        // 3) PING → PONG
        if (m.command == "PING") {
            std::string reply = "PONG ";
            reply.append(m.trailing);
            sendRaw(reply);
            continue;
        }

        // 4) RECONNECT → rebuild connection
        if (m.command == "RECONNECT") {
            std::cout << "[DEBUG] RECONNECT received, reconnecting…\n";
            boost::beast::error_code ec;
            socket_.close(ec);
            if (ec) std::cerr << "[TwitchBot] close error: " << ec.message() << "\n";
            connectWebSocket();
            continue;
        }

        // 5) PRIVMSG → strip leading '#', then dispatch
        if (m.command == "PRIVMSG" && !m.params.empty()) {
            auto rawChan = m.params[0];
            // remove the '#' Twitch IRC gives us
            std::string_view channel =
                (!rawChan.empty() && rawChan.front() == '#')
                ? rawChan.substr(1)
                : rawChan;
            onPrivMsg(channel, m);
            continue;
        }

        // 6) Other Twitch commands (optional logging)
        if (m.command == "CLEARCHAT" && !m.params.empty()) {
            std::cout << "[CLEARCHAT] channel=" << m.params[0]
                << " target="
                << (m.trailing.empty() ? "<all>" : m.trailing)
                << "\n";
        }
    }
}

void TwitchBot::onPrivMsg(std::string_view channel, IrcMessage const& m) {
    // extract 'user' from m.prefix (":user!...")
    auto excl = m.prefix.find('!');
    std::string_view user = m.prefix.substr(1, excl - 1);
    std::string_view text = m.trailing;

    // now dispatch commands (!join, !rank, etc.)
    if (!text.empty() && text.front() == '!')
        handleCommand(channel, user, text);

    for (auto& cb : chatListeners_)
        cb(std::string(channel),
            std::string(user),
            std::string(text));
}

void TwitchBot::handleCommand(std::string_view channel,
                              std::string_view user,
                              std::string_view content)
{
    auto space = content.find(' ');
    auto cmd   = content.substr(0,space);
    auto args  = (space==std::string_view::npos)
               ? std::string_view{}
               : content.substr(space+1);

    if (cmd=="!join") {
        cmdJoin(user);
    }
    else if (cmd=="!leave") {
        cmdLeave(user);
    }
    else if (cmd=="!setnickname" && channel==controlChannel_) {
        cmdSetNickname(channel,args);
    }
    else if (cmd == "!rank") {
        cmdRank(channel);    
    }
    else if (cmd=="!record" || cmd == "!recordcs") {
        int limit=100;
        if (!args.empty()) {
            try { limit=std::clamp(std::stoi(std::string(args)),1,100); }
            catch(...){}
        }
        cmdRecord(channel,limit);
    }
}

// ——— Command Handlers ——————————————————————————————————————

void TwitchBot::cmdJoin(std::string_view user) noexcept {
    joinChannel(user);
    channelNicks_[std::string(user)] = std::nullopt;
    saveChannels();
    sendRaw("PRIVMSG #"+controlChannel_
            +" :Joined "+std::string(user));
}

void TwitchBot::cmdLeave(std::string_view user) noexcept {
    leaveChannel(user);
    channelNicks_.erase(std::string(user));
    saveChannels();
    sendRaw("PRIVMSG #"+controlChannel_
            +" :Left "+std::string(user));
}

void TwitchBot::cmdSetNickname(std::string_view channel,
                               std::string_view nickname) noexcept
{
    channelNicks_[std::string(channel)] = std::string(nickname);
    saveChannels();
    sendRaw("PRIVMSG #"+std::string(channel)
            +" :FACEIT nickname set to "+std::string(nickname));
}

void TwitchBot::cmdRecord(std::string_view channel, int limit) {
    // 1) When the stream is offline, bail out immediately.
    auto startMsOpt = getStreamStart(channel);
    if (!startMsOpt) {
        sendRaw("PRIVMSG #" + std::string(channel) + " :Stream is offline");
        return;
    }

    // 2) Find the FACEIT nickname for this channel
    auto it = channelNicks_.find(std::string(channel));
    if (it == channelNicks_.end() || !it->second) {
        sendRaw("PRIVMSG #" + std::string(channel) +
                " :Set FACEIT nickname with !setnickname");
        return;
    }

    try {
        // 3) Fetch the player and current ELO
        auto player = faceitClient_
            .get_player_by_nickname(*it->second, "cs2");
        auto pid = player.at("player_id").get<std::string>();
        int currentElo = player
            .at("games").at("cs2")
            .at("faceit_elo").get<int>();

        // 4) Use the exact stream-start millisecond timestamp
        int64_t fromMs = startMsOpt->count();

        // 5) Fetch per-match stats since stream went live
        auto stats = faceitClient_.get_player_stats(
            pid,
            /*from_ts=*/ fromMs,
            /*to_ts=*/   std::nullopt,
            /*limit=*/   limit
        );

        // 6) Count wins and losses
        int wins = std::count_if(
            stats.begin(), stats.end(),
            [](auto const& m) {
                return m.at("stats")
                        .at("Result")
                        .get<std::string>() == "1";
            }
        );
        int losses = static_cast<int>(stats.size()) - wins;

        // 7) Fetch ELO history since stream went live and sort by time
        auto history = faceitClient_.get_elo_history(
            pid,
            /*size=*/  limit,
            /*page=*/  0,
            /*from_ts=*/fromMs,
            /*to_ts=*/ std::nullopt
        );
        std::sort(
            history.begin(), history.end(),
            [](auto const& a, auto const& b) {
                return a.at("date").get<int64_t>()
                     < b.at("date").get<int64_t>();
            }
        );

        // 8) Compute ELO delta
        int eloChange = 0;
        if (history.size() >= 2) {
            int first = std::stoi(history.front().at("elo").get<std::string>());
            int last  = std::stoi(history.back() .at("elo").get<std::string>());
            eloChange = last - first;
        }

        // 9) Send summary back to chat
        std::ostringstream oss;
        oss << "PRIVMSG #" << channel << " :"
            << wins << "W/" << losses << "L (" << stats.size() << ")"
            << " | Elo " << currentElo
            << (eloChange >= 0 ? " (+" : " (") << eloChange << ")";
        sendRaw(oss.str());
    }
    catch (const std::exception& e) {
        std::cerr << "[cmdRecord] error: " << e.what() << "\n";
    }
}

void TwitchBot::cmdRank(std::string_view channelSv) {
    const std::string channel{ channelSv };

    auto it = channelNicks_.find(channel);
    if (it == channelNicks_.end() || !it->second) {
        sendRaw("PRIVMSG #" + channel
            + " :No FACEIT nickname set. Use !setnickname first.");
        return;
    }

    try {
        auto player = faceitClient_.get_player_by_nickname(*it->second, "cs2");
        int elo = player["games"]["cs2"]["faceit_elo"].get<int>();

        struct LevelInfo { int level, minElo, maxElo; };
        constexpr LevelInfo kLevels[] = {
            {10, 2001, INT_MAX},
            { 9, 1751, 2000},
            { 8, 1531, 1750},
            { 7, 1351, 1530},
            { 6, 1201, 1350},
            { 5, 1051, 1200},
            { 4,  901, 1050},
            { 3,  751,  900},
            { 2,  501,  750},
            { 1,  100,  500}
        };

        int level = 1;
        for (auto const& info : kLevels) {
            if (elo >= info.minElo && elo <= info.maxElo) {
                level = info.level;
                break;
            }
        }

        sendRaw(
            "PRIVMSG #" + channel
            + " :Level " + std::to_string(level)
            + " : " + std::to_string(elo)
            + " Elo"
        );
    }
    catch (const std::exception& e) {
        std::cerr << "[cmdRank] failed to fetch Elo: " << e.what() << "\n";
    }
}

// ——— Helix Token & Stream Lookup ——————————————————————————————————

void TwitchBot::ensureHelixToken() {
    using namespace std::chrono;
    auto now = steady_clock::now();
    if (!helixToken_.empty() && now < helixExpiry_) return;

    try {
        std::string body = "client_id="+clientId_
                         + "&client_secret="+clientSecret_
                         + "&grant_type=client_credentials";

        auto resp = http_client::post(
            "id.twitch.tv", helix_port, "/oauth2/token",
            body,
            {{"Content-Type","application/x-www-form-urlencoded"}},
            ioc_, sslCtx_);

        auto j = json::parse(resp);
        helixToken_ = j.at("access_token").get<std::string>();
        helixExpiry_ = now + seconds{ j.at("expires_in").get<int>() };
    }
    catch(...) {
        helixToken_.clear();
    }
}

std::optional<std::chrono::milliseconds>
TwitchBot::getStreamStart(std::string_view channel) {
    ensureHelixToken();
    if (helixToken_.empty()) return std::nullopt;

    try {
        auto url = std::string("/helix/streams?user_login=")+std::string(channel);
        auto resp = http_client::get(
            helix_host, helix_port, url,
            {{"Client-ID",clientId_},
             {"Authorization","Bearer "+helixToken_}},
            ioc_, sslCtx_);

        auto j = json::parse(resp);
        auto& data = j.at("data");
        if (!data.is_array() || data.empty())
            return std::nullopt;

        std::tm tm{};
        std::istringstream ss(data[0].at("started_at").get<std::string>());
        ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
        if (ss.fail()) return std::nullopt;

        auto sec = portable_timegm(&tm);
        return std::chrono::milliseconds{sec*1000};
    }
    catch(...) {
        return std::nullopt;
    }
}

// ——— Persistence ——————————————————————————————————————————————

void TwitchBot::loadChannels() {
    std::ifstream in("channels.json");
    if (!in) return;
    json j; in >> j;
    for (auto& [c,n] : j.items()) {
        channelNicks_[c] = n.is_null()
            ? std::nullopt
            : std::optional<std::string>{n.get<std::string>()};
    }
}

void TwitchBot::saveChannels() {
    json j;
    for (auto& [c,mn] : channelNicks_)
        j[c] = mn;
    std::ofstream out("channels.json");
    out << j.dump(2);
}
