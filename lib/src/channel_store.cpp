#include "channel_store.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <system_error>

namespace twitch_bot {

ChannelStore::ChannelStore(std::filesystem::path filepath)
    : filename_(std::move(filepath))
{}

void ChannelStore::load()
{
    if (!std::filesystem::exists(filename_)) {
        return;
    }

    toml::table tbl;
    try {
        tbl = toml::parse_file(filename_.string());
    } catch (const toml::parse_error& err) {
        std::cerr << "[ChannelStore] Failed to parse TOML '"
                  << filename_ << "': " << err << "\n";
        return;
    } catch (const std::exception& ex) {
        std::cerr << "[ChannelStore] Error parsing '"
                  << filename_ << "': " << ex.what() << "\n";
        return;
    }

    channelData_.clear();
    channelData_.reserve(tbl.size());

    for (auto const& [key, val] : tbl) {
        if (auto const* chTbl = val.as_table()) {
            ChannelInfo info;
            if (auto node = chTbl->get("alias");
                node && node->is_string())
            {
                info.alias = node->value<std::string>();
            }
            channelData_.emplace(std::string(key), std::move(info));
        }
    }
}

void ChannelStore::save() const
{
    toml::table tbl;
    for (auto const& [ch, info] : channelData_) {
        toml::table entry;
        if (info.alias) {
            entry.insert("alias", *info.alias);
        }
        tbl.insert(ch, std::move(entry));
    }

    std::ostringstream oss;
    oss << tbl;
    const auto data = oss.str();

    const auto tmp = filename_.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) {
            std::cerr << "[ChannelStore] Cannot open '"
                      << tmp << "' for writing\n";
            return;
        }
        out << data;
        if (!out) {
            std::cerr << "[ChannelStore] Error writing to '"
                      << tmp << "'\n";
            return;
        }
    }

    std::error_code ec;
    std::filesystem::rename(tmp, filename_, ec);
    if (ec) {
        std::cerr << "[ChannelStore] Rename failed: "
                  << ec.message() << "\n";
        std::filesystem::remove(tmp, ec);
    }
}

void ChannelStore::addChannel(std::string_view channel)
{
    channelData_.try_emplace(std::string(channel), ChannelInfo{});
}

void ChannelStore::removeChannel(std::string_view channel)
{
    auto it = channelData_.find(channel);
    if (it != channelData_.end()) {
        channelData_.erase(it);
    }
}

std::optional<std::string> ChannelStore::getAlias(
    std::string_view channel) const
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
    if (auto it = channelData_.find(channel);
        it != channelData_.end())
    {
        it->second.alias = std::move(alias);
    }
}

std::vector<ChannelName> ChannelStore::allChannels() const
{
    std::vector<ChannelName> list;
    list.reserve(channelData_.size());
    for (auto const& [ch, _] : channelData_) {
        list.push_back(ch);
    }
    return list;
}

} // namespace twitch_bot
