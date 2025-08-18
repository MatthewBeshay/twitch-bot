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
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/strand.hpp>

// Toml++
#include <toml++/toml.hpp>

namespace app {

// Expected number of channels and load factor target for the map.
inline constexpr std::size_t kDefaultExpectedChannels = 256;
inline constexpr float kChannelDataMaxLoadFactor = 0.5F;

// Per-channel metadata.
struct ChannelInfo {
    std::optional<std::string> alias;
};

// Thread-safe store of Twitch channels and metadata, persisted to TOML.
// File layout:
//   [<channel>]
//   alias = "..."
class ChannelStore
{
public:
    ChannelStore(boost::asio::any_io_executor executor,
                 const std::filesystem::path& filepath = "channels.toml",
                 std::size_t expected_channels = kDefaultExpectedChannels)
        : strand_{std::move(executor)}, filename_{filepath}, save_timer_{strand_}
    {
        channel_data_.reserve(expected_channels);
        channel_data_.max_load_factor(kChannelDataMaxLoadFactor);
    }

    ~ChannelStore();

    ChannelStore(const ChannelStore&) = delete;
    ChannelStore& operator=(const ChannelStore&) = delete;
    ChannelStore(ChannelStore&&) = delete;
    ChannelStore& operator=(ChannelStore&&) = delete;

    // Load from disk (best effort; leaves map unchanged on parse errors).
    void load();

    // Schedule a debounced save if data changed.
    void save() const noexcept;

    // --- thread-safe API -----------------------------------------------------

    void add_channel(std::string_view channel);
    void remove_channel(std::string_view channel) noexcept;
    [[nodiscard]] bool contains(std::string_view channel) const noexcept;

    // Returns a copy (no dangling).
    [[nodiscard]] std::optional<std::string> get_alias(std::string_view channel) const;
    void set_alias(std::string_view channel, std::optional<std::string> alias);

    // Copies current channel names (lowercase) into out; reuses capacity.
    void channel_names(std::vector<std::string>& out) const;

private:
    [[nodiscard]] toml::table build_table() const;
    void perform_save() const noexcept;

    static std::string to_lower_ascii(std::string_view s)
    {
        std::string out;
        out.reserve(s.size());
        for (unsigned char c : s)
            out.push_back((c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : char(c));
        return out;
    }

    mutable std::shared_mutex data_mutex_;
    std::unordered_map<std::string, ChannelInfo> channel_data_; // lowercase keys

    boost::asio::strand<boost::asio::any_io_executor> strand_;
    const std::filesystem::path filename_;

    // Debounced writeback state.
    mutable std::atomic<bool> dirty_{false};
    mutable std::atomic<bool> timer_scheduled_{false};
    mutable boost::asio::steady_timer save_timer_;
};

} // namespace app
