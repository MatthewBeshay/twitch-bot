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

/// In-memory store of channels and optional aliases.
/// Persisted atomically to a TOML file at construction path.
class ChannelStore {
public:
    /// Construct a store backed by 'filepath' (default "channels.toml").
    explicit ChannelStore(std::filesystem::path filepath = "channels.toml");

    /// Load from disk. On parse or I/O error, logs and leaves data unchanged.
    void load();

    /// Save to disk atomically (write to ".tmp" then rename). Logs on failure.
    void save() const;

    /// Add channel (no alias). No-op if already present.
    void addChannel(std::string_view channel);

    /// Remove channel. No-op if not present.
    void removeChannel(std::string_view channel);

    /// @return true if the channel is already in the store.
    bool contains(std::string_view channel) const noexcept {
        return channelData_.find(channel) != channelData_.end();
    }

    /// @return Alias if set and channel exists; otherwise std::nullopt.
    std::optional<std::string> getAlias(std::string_view channel) const;

    /// Set or clear alias for existing channel. No-op if not present.
    void setAlias(std::string_view channel,
                  std::optional<std::string> alias);

    /// @return List of all stored channel names.
    std::vector<ChannelName> allChannels() const;

private:
    std::filesystem::path filename_;
    std::unordered_map<
        std::string,
        ChannelInfo,
        TransparentStringHash,
        TransparentStringEq
    > channelData_;
};

} // namespace twitch_bot
