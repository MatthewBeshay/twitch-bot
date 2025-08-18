// C++ Standard Library
#include <fstream>
#include <iostream>
#include <utility>

// Toml++
#include <toml++/toml.hpp>

// App
#include <app/app_channel_store.hpp>

namespace app {

AppChannelStore::AppChannelStore(std::filesystem::path path) noexcept
    : path_(std::move(path)) {}

std::string AppChannelStore::to_lower_ascii(std::string_view s) {
    std::string out; out.reserve(s.size());
    for (unsigned char c : s)
        out.push_back((c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : char(c));
    return out;
}

void AppChannelStore::load() noexcept {
    per_channel_.clear();
    if (!std::filesystem::exists(path_)) return;

    try {
        toml::table root = toml::parse_file(path_.string());
        if (auto* chs = root.get_as<toml::table>("channels")) {
            for (auto&& [chan_key, chan_node] : *chs) {
                if (!chan_node.is_table()) continue;

                const std::string chan = to_lower_ascii(chan_key.str());
                FaceitSettings s{};

                if (auto* faceit_tbl = chan_node.as_table()->get_as<toml::table>("faceit")) {
                    if (auto* n = faceit_tbl->get("nickname")) {
                        if (auto v = n->value<std::string>()) s.nickname = std::move(*v);
                    }
                    if (auto* id = faceit_tbl->get("player_id")) {
                        if (auto v = id->value<std::string>()) s.player_id = std::move(*v);
                    }
                }

                per_channel_.emplace(chan, std::move(s));
            }
        }
    } catch (const toml::parse_error& e) {
        std::cerr << "[AppChannelStore] TOML parse error: " << e << '\n';
    } catch (const std::exception& e) {
        std::cerr << "[AppChannelStore] load() error: " << e.what() << '\n';
    }
}

void AppChannelStore::save() const noexcept {
    try {
        toml::table root;
        toml::table chs;

        for (const auto& [chan, s] : per_channel_) {
            toml::table chan_tbl;
            toml::table faceit_tbl;

            if (s.nickname)  faceit_tbl.insert_or_assign("nickname", *s.nickname);
            if (s.player_id) faceit_tbl.insert_or_assign("player_id", *s.player_id);

            chan_tbl.insert_or_assign("faceit", std::move(faceit_tbl));
            chs.insert_or_assign(chan, std::move(chan_tbl));
        }

        root.insert_or_assign("channels", std::move(chs));

        std::ofstream ofs(path_, std::ios::binary | std::ios::trunc);
        if (!ofs) {
            std::cerr << "[AppChannelStore] cannot open " << path_ << " for write\n";
            return;
        }
        ofs << root;
        if (!ofs) {
            std::cerr << "[AppChannelStore] write failed for " << path_ << '\n';
        }
    } catch (const std::exception& e) {
        std::cerr << "[AppChannelStore] save() error: " << e.what() << '\n';
    }
}

bool AppChannelStore::contains(std::string_view channel) const noexcept {
    return per_channel_.find(to_lower_ascii(channel)) != per_channel_.end();
}

void AppChannelStore::erase(std::string_view channel) noexcept {
    per_channel_.erase(to_lower_ascii(channel));
}

std::optional<std::string> AppChannelStore::get_faceit_nick(std::string_view channel) const {
    const auto it = per_channel_.find(to_lower_ascii(channel));
    if (it == per_channel_.end() || !it->second.nickname) return std::nullopt;
    return it->second.nickname;
}

std::optional<std::string> AppChannelStore::get_faceit_id(std::string_view channel) const {
    const auto it = per_channel_.find(to_lower_ascii(channel));
    if (it == per_channel_.end() || !it->second.player_id) return std::nullopt;
    return it->second.player_id;
}

void AppChannelStore::set_faceit_nick(std::string_view channel, std::string nick) {
    per_channel_[to_lower_ascii(channel)].nickname = std::move(nick);
}

void AppChannelStore::set_faceit_id(std::string_view channel, std::string id) {
    per_channel_[to_lower_ascii(channel)].player_id = std::move(id);
}

void AppChannelStore::clear_faceit_id(std::string_view channel) {
    per_channel_[to_lower_ascii(channel)].player_id.reset();
}

} // namespace app
