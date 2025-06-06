#include "channel_store.hpp"

#include <fstream>
#include <iostream>
#include <filesystem>
#include <sstream>

#include <toml++/toml.hpp>

namespace twitch_bot {

ChannelStore::ChannelStore(std::filesystem::path filepath)
    : filename_(std::move(filepath))
{}

void ChannelStore::load()
{
    namespace fs = std::filesystem;

    // If the file does not exist, there is nothing to load.
    if (!fs::exists(filename_)) {
        return;
    }

    toml::table tbl;
    try {
        tbl = toml::parse_file(filename_.string());
    }
    catch (const toml::parse_error& err) {
        std::cerr << "[ChannelStore] Failed to parse TOML '"
                  << filename_ << "':\n"
                  << err << "\n";
        return;
    }
    catch (const std::exception& ex) {
        std::cerr << "[ChannelStore] Error while parsing TOML '"
                  << filename_ << "': " << ex.what() << "\n";
        return;
    }

    // Replace in-memory data with on-disk contents.
    channelData_.clear();
    channelData_.reserve(tbl.size());

    // Each child table [channel] represents one channel entry.
    for (auto const& [key, val] : tbl) {
        if (auto const* channelTable = val.as_table()) {
            ChannelInfo info;

            // If an "alias" key is present and is a string, store it.
            if (auto aliasNode = channelTable->get("alias");
                aliasNode && aliasNode->is_string())
            {
                info.alias = aliasNode->value<std::string>();
            } else {
                info.alias = std::nullopt;
            }

            // Insert the channel name (key) and its info into the map.
            channelData_.emplace(std::string{key}, std::move(info));
        }
        // Ignore any non-table entries.
    }
}

void ChannelStore::save() const
{
    toml::table tbl;

    // Build a TOML table for each channel.
    for (auto const& [channel, info] : channelData_) {
        toml::table channelTbl;
        if (info.alias) {
            channelTbl.insert("alias", *info.alias);
        }
        tbl.insert(channel, std::move(channelTbl));
    }

    // Serialize the entire table to a string.
    std::ostringstream oss;
    oss << tbl;
    const std::string serialized = oss.str();

    // Write to a temporary file, then rename to enforce atomic-ish update.
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

void ChannelStore::addChannel(std::string_view channel)
{
    // Inserts a new channel if it does not already exist.
    channelData_.try_emplace(std::string{channel}, ChannelInfo{});
}

void ChannelStore::removeChannel(std::string_view channel)
{
    // Find by heterogeneous lookup; if found, erase it.
    auto it = channelData_.find(channel);
    if (it == channelData_.end()) {
        return;
    }
    channelData_.erase(it);
}

std::vector<ChannelName> ChannelStore::allChannels() const
{
    std::vector<ChannelName> result;
    result.reserve(channelData_.size());
    for (auto const& [chan, _info] : channelData_) {
        result.push_back(chan);
    }
    return result;
}

std::optional<std::string> ChannelStore::getAlias(std::string_view channel) const
{
    auto it = channelData_.find(channel);
    if (it == channelData_.end()) {
        return std::nullopt;
    }
    return it->second.alias;
}

void ChannelStore::setAlias(std::string_view channel,
                            std::optional<std::string> alias)
{
    auto it = channelData_.find(channel);
    if (it == channelData_.end()) {
        return;
    }
    it->second.alias = std::move(alias);
}

} // namespace twitch_bot
