#pragma once

#include "utils/transparent_string.hpp"

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <optional>
#include <filesystem>

#include <toml++/toml.hpp>


namespace twitch_bot {

using ChannelName = std::string;

/// Holds per-channel metadata: optional alias and optional FaceIT nickname.
struct ChannelInfo {
    std::optional<std::string> alias;
    std::optional<std::string> faceit;
};

/**
 * @brief A simple in-process "database" of which channels we're in, plus two
 *        optional strings per channel:
 *         * alias  (set via !setnickname)
 *         * faceit (set via !setfaceit)
 *
 * Internally stored in a TOML file (default: "channels.toml"):
 *
 *   [somechannel]
 *   alias  = "someAlias"
 *   faceit = "FaceITName"
 *
 *   [otherchannel]
 *   alias  = "OtherAlias"
 *   # no "faceit" key -> std::nullopt
 */
class ChannelStore {
public:
    explicit ChannelStore(boost::asio::any_io_executor executor,
                          const std::filesystem::path &filepath = "channels.toml",
                          std::size_t expected_channels = 256)
        : strand_{std::move(executor)}, filename_{filepath}, save_timer_{strand_}
    {
        channel_data_.reserve(expected_channels);
        channel_data_.max_load_factor(0.5f);
    }

    ~ChannelStore();

    ChannelStore(const ChannelStore &) = delete;
    ChannelStore &operator=(const ChannelStore &) = delete;

    /**
     * @brief Read `filename_` (if it exists) and parse via toml++,
     *        populating `channelData_`.
     * @post  On success, `channelData_` is replaced with the on-disk contents.
     * @post  On parse error or I/O failure, `channelData_` is left unchanged
     *        and an error is logged.
     */
    void load();

    /**
     * @brief Write the current state of `channelData_` out to `filename_`
     *        as a TOML table (atomically, by writing to a ".tmp" then renaming).
     * @post  On success, the file at `filename_` matches `channelData_`.
     *        On failure, logs an error.
     */
    void save() const;

    /**
     * @brief Add a new channel (with no initial alias or faceit nick).
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
     * @brief Optionally get the stored "alias" for a given channel.
     * @param channel  Name of the channel to query.
     * @return `std::nullopt` if the channel is not present or has no alias,
     *         otherwise the alias string.
     */
    std::optional<std::string> getAlias(std::string_view channel) const;

    /**
     * @brief Set (or clear) the stored "alias" for a given channel.
     * @param channel  Name of the channel to modify.
     * @param alias    If non-nullopt, sets the alias; if `std::nullopt`, clears it.
     * @post  If the channel is not already present, does nothing.
     */
    void setAlias(std::string_view channel, std::optional<std::string> alias);

    /**
     * @brief Optionally get the stored "faceit" nickname for a given channel.
     * @param channel  Name of the channel to query.
     * @return `std::nullopt` if the channel is not present or has no faceit nick,
     *         otherwise the faceit string.
     */
    std::optional<std::string> getFaceitNick(std::string_view channel) const;

    /**
     * @brief Set (or clear) the stored "faceit" nickname for a given channel.
     * @param channel  Name of the channel to modify.
     * @param faceit   If non-nullopt, sets the faceit nick; if `std::nullopt`, clears it.
     * @post  If the channel is not already present, does nothing.
     */
    void setFaceitNick(std::string_view channel, std::optional<std::string> faceit);

private:
    std::filesystem::path filename_;

    // heterogeneous-lookup map: key = channel name, value = ChannelInfo
    // Now using TransparentStringHash / TransparentStringEq for zero-overhead lookups.
    std::unordered_map<
        std::string,
        ChannelInfo,
        TransparentStringHash,
        TransparentStringEq
    > channelData_;
};

} // namespace twitch_bot
