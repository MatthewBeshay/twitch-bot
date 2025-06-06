#pragma once

#include "utils/transparent_string.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <toml++/toml.hpp>

namespace twitch_bot {

using ChannelName = std::string;

/// Per-channel metadata (currently only an optional alias).
struct ChannelInfo {
    std::optional<std::string> alias;
};

/// Simple in-process store of channels and (optional) aliases.
/// Persisted to a TOML file, e.g.:
///
/// [somechannel]
/// alias = "someAlias"
///
/// [otherchannel]
/// # no alias -> std::nullopt
class ChannelStore {
public:
    /// Construct a ChannelStore that reads/writes to 'filepath'.
    /// @param filepath  Path to TOML file (default: "channels.toml").
    explicit ChannelStore(std::filesystem::path filepath = "channels.toml");

    ~ChannelStore() noexcept = default;
    ChannelStore(const ChannelStore&) = delete;
    ChannelStore& operator=(const ChannelStore&) = delete;
    ChannelStore(ChannelStore&&) noexcept = default;
    ChannelStore& operator=(ChannelStore&&) noexcept = default;

    /// Load the TOML file into memory. On parse or I/O error, logs and leaves data unchanged.
    void load();

    /// Save current data to disk atomically (write to ".tmp", then rename). Logs on failure.
    void save() const;

    /// Add a new channel (no alias). If already present, does nothing.
    void addChannel(std::string_view channel);

    /// Remove a channel. If not present, does nothing.
    void removeChannel(std::string_view channel);

    /// @return Vector of all stored channel names.
    std::vector<ChannelName> allChannels() const;

    /// @return Alias if set and channel exists; otherwise std::nullopt.
    std::optional<std::string> getAlias(std::string_view channel) const;

    /// Set or clear alias for an existing channel. If channel not present, does nothing.
    void setAlias(std::string_view channel,
                  std::optional<std::string> alias);

private:
    std::filesystem::path filename_;

    // Heterogeneous-lookup map: key = channel name, value = ChannelInfo.
    // TransparentStringHash/Eq allow zero-overhead lookup with string_view.
    std::unordered_map<
        std::string,
        ChannelInfo,
        TransparentStringHash,
        TransparentStringEq
    > channelData_;
};

} // namespace twitch_bot
