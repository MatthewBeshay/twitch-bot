#pragma once

#include "utils/transparent_string.hpp"

#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <boost/asio.hpp>

#include <toml++/toml.hpp>

namespace twitch_bot {

using ChannelName = std::string;

struct ChannelInfo {
    std::optional<std::string> alias;
};

// Maintains in-memory channel metadata and persists it atomically
// to a TOML file via the given Asio executor. All operations are
// safe to call from any thread.
class ChannelStore {
public:
    // Supply an Asio executor for serialised file I/O and a file path.
    explicit ChannelStore(boost::asio::any_io_executor executor,
                          std::filesystem::path filepath = "channels.toml");

    // Blocks until all pending save tasks complete.
    ~ChannelStore();

    ChannelStore(const ChannelStore&) = delete;
    ChannelStore& operator=(const ChannelStore&) = delete;

    // Load from disk; on failure leaves existing data intact.
    void load();

    // Snapshot current data and schedule a non-blocking save.
    void save() const noexcept;

    // Modifiers and accessors; noexcept or cheap by design.
    void addChannel(std::string_view channel) noexcept;
    void removeChannel(std::string_view channel) noexcept;
    bool contains(std::string_view channel) const noexcept;
    std::optional<std::string_view> getAlias(std::string_view channel) const noexcept;
    void setAlias(std::string_view channel,
                  std::optional<std::string> alias) noexcept;
    std::vector<std::string_view> channelNames() const noexcept;

private:
    // Build a TOML table representing current in-memory state.
    toml::table buildTable() const;

    mutable std::shared_mutex data_mutex_;
    std::unordered_map<std::string, ChannelInfo,
                       TransparentStringHash,
                       TransparentStringEq> channelData_;

    // Strand serialises all file-write handlers on the supplied executor.
    boost::asio::strand<boost::asio::any_io_executor> strand_;
    const std::filesystem::path filename_;
};

} // namespace twitch_bot
