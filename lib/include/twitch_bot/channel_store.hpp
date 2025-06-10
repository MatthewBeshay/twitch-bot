#pragma once

#include "utils/transparent_string.hpp"

#include <atomic>
#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

#include <toml++/toml.hpp>

namespace twitch_bot {

using ChannelName = std::string;

// Information associated with a channel.
struct ChannelInfo {
    std::optional<std::string> alias;
};

// Manages channel metadata in memory and persists it to a TOML file.
// All operations are thread-safe. File I/O is serialized via Asio.
class ChannelStore {
public:
    // Construct with an Asio executor for serialized file I/O and a TOML file path.
    explicit ChannelStore(boost::asio::any_io_executor executor,
                          std::filesystem::path filepath = "channels.toml");

    // Blocks until all pending save tasks complete.
    ~ChannelStore();

    ChannelStore(const ChannelStore&) = delete;
    ChannelStore& operator=(const ChannelStore&) = delete;

    // Load from disk; on failure leaves existing data intact.
    void load();

    // Snapshot current data and schedule a debounced save.
    void save() const noexcept;

    // Modifiers and accessors; noexcept or cheap by design.
    void addChannel(std::string_view channel) noexcept;
    void removeChannel(std::string_view channel) noexcept;
    bool contains(std::string_view channel) const noexcept;
    std::optional<std::string_view> getAlias(std::string_view channel) const noexcept;
    void setAlias(std::string_view channel, std::optional<std::string> alias) noexcept;
    std::vector<std::string_view> channelNames() const noexcept;

private:
    // Build a TOML table representing current in-memory state.
    toml::table buildTable() const;

    // Write metadata snapshot to disk; invoked after debounce timer fires.
    void performSave() const noexcept;

    mutable std::shared_mutex data_mutex_;
    std::unordered_map<std::string, ChannelInfo,
                       TransparentStringHash,
                       TransparentStringEq> channelData_;

    boost::asio::strand<boost::asio::any_io_executor> strand_;
    const std::filesystem::path filename_;

    mutable std::atomic<bool>         dirty_{ false };
    mutable std::atomic<bool>         timerScheduled_{ false };
    mutable boost::asio::steady_timer saveTimer_;
};

} // namespace twitch_bot
