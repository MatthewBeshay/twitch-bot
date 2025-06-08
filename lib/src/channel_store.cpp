#include "channel_store.hpp"

#include <fstream>
#include <future>
#include <iostream>
#include <sstream>
#include <system_error>

namespace twitch_bot {

ChannelStore::ChannelStore(boost::asio::any_io_executor executor,
                           std::filesystem::path filepath)
  : strand_(std::move(executor)),
    filename_(std::move(filepath))
{}

ChannelStore::~ChannelStore() {
    // Post a no-op on the strand and wait so that all pending saves complete.
    std::promise<void> p;
    auto f = p.get_future();
    boost::asio::post(strand_,
        [pr = std::move(p)]() mutable {
            pr.set_value();
        });
    f.wait();
}

void ChannelStore::load() {
    // If no file exists yet, start with an empty store.
    if (!std::filesystem::exists(filename_)) {
        return;
    }

    toml::table tbl;
    try {
        tbl = toml::parse_file(filename_.string());
    }
    catch (const toml::parse_error& e) {
        std::cerr << "[ChannelStore] parse error: " << e << "\n";
        return;
    }
    catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "[ChannelStore] filesystem error: " << e.what() << "\n";
        return;
    }

    std::unique_lock lock(data_mutex_);
    channelData_.clear();
    channelData_.reserve(tbl.size());

    for (auto& [key, value] : tbl) {
        if (auto table = value.as_table()) {
            ChannelInfo info;
            if (auto node = table->get("alias");
                node && node->is_string())
            {
                info.alias = node->value<std::string>();
            }
            channelData_.emplace(key, std::move(info));
        }
    }
}

void ChannelStore::save() const noexcept {
    // --- snapshot data under lock ---
    toml::table tbl;
    {
        std::shared_lock lock(data_mutex_);
        tbl = buildTable();
    }

    // --- serialise outside lock ---
    std::ostringstream oss;
    oss << tbl;
    auto data = oss.str();
    auto filename = filename_;

    // Schedule the write on the strand for serialised I/O.
    boost::asio::post(strand_,
        [data = std::move(data), filename = std::move(filename)]() {
            const auto temp = filename.string() + ".tmp";

            {
                // 1) Write to a temporary file...
                std::ofstream out(temp, std::ios::trunc);
                if (!out) {
                    std::cerr << "[ChannelStore] cannot open " << temp << "\n";
                    return;
                }
                out << data;
                if (!out) {
                    std::cerr << "[ChannelStore] write failed " << temp << "\n";
                    return;
                }
            } // <-- 'out' is destroyed here, closing the file handle

            // 2) Atomically replace the old file.
            std::error_code ec;
            std::filesystem::rename(temp, filename, ec);
            if (ec) {
                std::cerr << "[ChannelStore] rename failed: "
                          << ec.message() << "\n";
                std::filesystem::remove(temp, ec);
            }
        });
}

void ChannelStore::addChannel(std::string_view channel) noexcept {
    std::unique_lock lock(data_mutex_);
    channelData_.try_emplace(std::string(channel), ChannelInfo{});
}

void ChannelStore::removeChannel(std::string_view channel) noexcept {
    std::unique_lock lock(data_mutex_);
    channelData_.erase(channel);
}

bool ChannelStore::contains(std::string_view channel) const noexcept {
    std::shared_lock lock(data_mutex_);
    return channelData_.find(channel) != channelData_.end();
}

std::optional<std::string_view> ChannelStore::getAlias(
    std::string_view channel) const noexcept
{
    std::shared_lock lock(data_mutex_);
    auto it = channelData_.find(channel);
    if (it != channelData_.end() && it->second.alias) {
        return *it->second.alias;
    }
    return std::nullopt;
}

void ChannelStore::setAlias(std::string_view channel,
                            std::optional<std::string> alias) noexcept
{
    std::unique_lock lock(data_mutex_);
    if (auto it = channelData_.find(channel); it != channelData_.end()) {
        it->second.alias = std::move(alias);
    }
}

std::vector<std::string_view> ChannelStore::channelNames() const noexcept {
    std::shared_lock lock(data_mutex_);
    std::vector<std::string_view> names;
    names.reserve(channelData_.size());
    for (auto& [key, info] : channelData_) {
        names.push_back(key);
    }
    return names;
}

toml::table ChannelStore::buildTable() const {
    toml::table tbl;
    std::shared_lock lock(data_mutex_);
    for (auto& [key, info] : channelData_) {
        toml::table entry;
        if (info.alias) {
            entry.insert("alias", *info.alias);
        }
        tbl.insert(key, std::move(entry));
    }
    return tbl;
}

} // namespace twitch_bot
