/*
Module Name:
- channel_store.hpp

Abstract:
- Thread safe store for Twitch channels and per-channel metadata.
- Loads from a TOML file, serves queries and updates under a shared mutex,
  and persists changes using a debounced async save on an Asio strand.
- Map uses transparent hashing for string_view interoperability and a low
  load factor to keep lookups cheap.
*/
#pragma once

// C++ Standard Library
#include <atomic>
#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Boost.Asio
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

// Toml++
#include <toml++/toml.hpp>

// Core
#include <tb/utils/attributes.hpp>
#include <tb/utils/transparent_string_hash.hpp>

namespace twitch_bot {

// Default bucket expectation for the channel map.
inline constexpr std::size_t kDefaultExpectedChannels = 256;

// Load-factor threshold that triggers rehashing.
inline constexpr float kChannelDataMaxLoadFactor = 0.5F;

// Per-channel metadata.
struct ChannelInfo {
    std::optional<std::string> alias; // user friendly name
};

// Load, cache and persist a set of Twitch channels and their aliases.
// All public operations are safe to call concurrently.
class ChannelStore
{
public:
    // Channels are read from filepath on first load().
    explicit ChannelStore(boost::asio::any_io_executor executor,
                          const std::filesystem::path& filepath = "channels.toml",
                          std::size_t expected_channels = kDefaultExpectedChannels)
        : strand_{std::move(executor)}
        , filename_{filepath}
        , dirty_{false}
        , timer_scheduled_{false}
        , save_timer_{strand_}
    {
        channel_data_.reserve(expected_channels);
        channel_data_.max_load_factor(kChannelDataMaxLoadFactor);
    }

    ~ChannelStore();

    ChannelStore(const ChannelStore&) = delete;
    ChannelStore& operator=(const ChannelStore&) = delete;
    ChannelStore(ChannelStore&&) = delete;
    ChannelStore& operator=(ChannelStore&&) = delete;

    // Load channel data from disk.
    void load();

    // Persist channel data if modified (debounced on a timer).
    void save() const noexcept;

    // --- thread safe modifiers and queries -------------------------------------

    // Add a channel (idempotent). May allocate.
    void add_channel(std::string_view channel);

    // Remove a channel (noop if absent).
    void remove_channel(std::string_view channel) noexcept;

    // True if channel exists.
    [[nodiscard]] bool contains(std::string_view channel) const noexcept;

    // Alias for channel, if one exists. Returns a safe copy to avoid dangling.
    [[nodiscard]] std::optional<std::string> get_alias(std::string_view channel) const;

    // Set or clear an alias for an existing channel.
    void set_alias(std::string_view channel, std::optional<std::string> alias);

    // Copy the current channel names into out (capacity reused).
    // Copies to strings to avoid dangling if the map mutates after return.
    void channel_names(std::vector<std::string_view>& out) const;

private:
    [[nodiscard]] toml::table build_table() const;
    void perform_save() const noexcept;

    mutable std::shared_mutex data_mutex_;
    std::unordered_map<std::string,
                       ChannelInfo,
                       TransparentBasicStringHash<char>,
                       TransparentBasicStringEq<char>>
        channel_data_;

    boost::asio::strand<boost::asio::any_io_executor> strand_;
    const std::filesystem::path filename_;

    // writeback state (debounced)
    mutable std::atomic<bool> dirty_{false};
    mutable std::atomic<bool> timer_scheduled_{false};
    mutable boost::asio::steady_timer save_timer_;
};

} // namespace twitch_bot
