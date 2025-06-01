#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <filesystem>

namespace twitch_bot {

using ChannelName = std::string;
using NickName    = std::string;

/**
 * @brief A simple in-process "database" of which channels we're in, and
 *        optionally what the last known "nick" (or similar) was.
 *
 * Internally stored in a TOML file (default: "channels.toml"):
 *
 *   [somechannel]
 *   nick = "lastNick"
 *
 *   [otherchannel]
 *   # no "nick" key -> interpreted as std::nullopt
 */
class ChannelStore {
public:
    /**
     * @brief Construct a ChannelStore that reads/writes from the given file path.
     * @param filepath  Path to the TOML file to load/save. Defaults to "channels.toml".
     */
    explicit ChannelStore(std::filesystem::path filepath = "channels.toml");

    ~ChannelStore() noexcept = default;

    ChannelStore(const ChannelStore&) = delete;
    ChannelStore& operator=(const ChannelStore&) = delete;
    ChannelStore(ChannelStore&&) noexcept = default;
    ChannelStore& operator=(ChannelStore&&) noexcept = default;

    /**
     * @brief Read `filename_` (if it exists) and parse via Toml++, populating `channelNicks_`.
     * @post  On success, `channelNicks_` is replaced with the on-disk contents.
     * @post  On parse error or I/O failure, `channelNicks_` is left unchanged and an error is logged.
     */
    void load();

    /**
     * @brief Write the current state of `channelNicks_` out to `filename_`
     *        as a TOML table (atomically, by writing to a ".tmp" then renaming).
     * @post  On success, the file at `filename_` matches `channelNicks_`. On failure, logs an error.
     */
    void save() const;

    /**
     * @brief Add a new channel (with no initial nickname).
     * @param channel  Name of the channel to add. If already present, does nothing.
     */
    void addChannel(std::string_view channel);

    /**
     * @brief Remove a channel entirely from the store.
     * @param channel  Name of the channel to remove. If not present, does nothing.
     */
    void removeChannel(std::string_view channel);

    /**
     * @brief Return a vector of all channel names currently stored.
     * @return A list of channel names.
     */
    std::vector<ChannelName> allChannels() const;

    /**
     * @brief Optionally get the saved "nick" for a given channel.
     * @param channel  Name of the channel to query.
     * @return `std::nullopt` if the channel is not present or has no nick, otherwise the nick string.
     */
    std::optional<NickName> getNick(std::string_view channel) const;

    /**
     * @brief Set (or clear) the saved "nick" for a given channel.
     * @param channel  Name of the channel to modify.
     * @param nick     If non-empty, sets the nick; if `std::nullopt`, clears it.
     * @post  If the channel is not already present, does nothing.
     */
    void setNick(std::string_view channel, std::optional<NickName> nick);

private:
    std::filesystem::path                                           filename_;
    std::unordered_map<ChannelName, std::optional<NickName>>        channelNicks_;
};

} // namespace twitch_bot
