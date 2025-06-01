#include "channel_store.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <filesystem>

#include <toml++/toml.hpp>

namespace twitch_bot {

ChannelStore::ChannelStore(std::filesystem::path filepath)
    : filename_(std::move(filepath))
{ }

void ChannelStore::load() {
    namespace fs = std::filesystem;

    if (!fs::exists(filename_)) {
        return;  // no file -> nothing to load
    }

    toml::table tbl;
    try {
        tbl = toml::parse_file(filename_.string());
    }
    catch (const toml::parse_error& err) {
        // Print the parse_error (which includes line/column info)
        std::cerr << "[ChannelStore] Failed to parse TOML '"
                  << filename_ << "':\n"
                  << err << "\n";
        return;
    }
    catch (const std::exception& ex) {
        std::cerr << "[ChannelStore] Unexpected error while parsing TOML '"
                  << filename_ << "': " << ex.what() << "\n";
        return;
    }

    // Reserve space in our unordered_map before inserting
    channelNicks_.clear();
    channelNicks_.reserve(tbl.size());

    for (auto const& [key, val] : tbl) {
        if (auto const* channelTable = val.as_table()) {
            if (auto optNickNode = channelTable->get("nick");
                optNickNode && optNickNode->is_string())
            {
                channelNicks_.emplace(
                    key,
                    optNickNode->value<std::string>()
                );
            }
            else {
                channelNicks_.emplace(key, std::nullopt);
            }
        }
        else {
            // ignore any non-table entries
        }
    }
}

void ChannelStore::save() const {
    toml::table tbl;

    for (auto const& [channel, optNick] : channelNicks_) {
        toml::table channelTbl;
        if (optNick) {
            channelTbl.insert("nick", *optNick);
        }
        tbl.insert(channel, std::move(channelTbl));
    }

    // Serialize the table by streaming it into a std::ostringstream
    std::ostringstream oss;
    oss << tbl;
    const std::string serialized = oss.str();

    // Build a temporary filename (string) for atomic overwrite
    const std::string tmpName = filename_.string() + ".tmp";
    {
        std::ofstream out(tmpName, std::ios::trunc);
        if (!out) {
            std::cerr << "[ChannelStore] Cannot open '"
                      << tmpName << "' for writing\n";
            return;
        }
        out << serialized;
        if (!out) {
            std::cerr << "[ChannelStore] Error while writing to '"
                      << tmpName << "'\n";
            return;
        }
    }

    std::error_code ec;
    std::filesystem::rename(std::filesystem::path{tmpName}, filename_, ec);
    if (ec) {
        std::cerr << "[ChannelStore] Failed to overwrite '"
                  << filename_ << "' with '" << tmpName
                  << "': " << ec.message() << "\n";
        std::filesystem::remove(std::filesystem::path{tmpName}, ec);
    }
}

void ChannelStore::addChannel(std::string_view channel) {
    channelNicks_.try_emplace(std::string{channel}, std::nullopt);
}

void ChannelStore::removeChannel(std::string_view channel) {
    channelNicks_.erase(std::string{channel});
}

std::vector<ChannelName> ChannelStore::allChannels() const {
    std::vector<ChannelName> result;
    result.reserve(channelNicks_.size());
    for (auto const& [chan, _] : channelNicks_) {
        result.push_back(chan);
    }
    return result;
}

std::optional<NickName> ChannelStore::getNick(std::string_view channel) const {
    auto it = channelNicks_.find(std::string{channel});
    if (it == channelNicks_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void ChannelStore::setNick(std::string_view channel, std::optional<NickName> nick) {
    auto it = channelNicks_.find(std::string{channel});
    if (it == channelNicks_.end()) {
        return;
    }
    it->second = std::move(nick);
}

} // namespace twitch_bot
