#pragma once

/*
Module: channel_store.hpp

Purpose:
- Thread-safe store of Twitch channels and simple metadata, persisted to TOML.

Why:
- Multiple readers are common while edits are rare, so a shared_mutex fits well.
- File writes are debounced on a strand-bound timer to batch updates and avoid
  concurrent writes without global locks.
- Channel keys are stored lowercase for consistent lookups and to match Twitch.
*/

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

namespace app
{

    // Map sizing hints to minimise rehashing under typical loads.
    inline constexpr std::size_t kDefaultExpectedChannels = 256;
    inline constexpr float kChannelDataMaxLoadFactor = 0.5F;

    struct ChannelInfo
    {
        std::optional<std::string> alias; // optional user-facing name
    };

    // Thread-safe channel store persisted to TOML.
    // File layout:
    //   [<channel>]
    //   alias = "..."
    class ChannelStore
    {
    public:
        // Construct and pre-size the map. The timer is bound to strand_ so all
        // deferred saves are serialised without extra locking.
        ChannelStore(boost::asio::any_io_executor executor,
                     const std::filesystem::path& filepath = "channels.toml",
                     std::size_t expected_channels = kDefaultExpectedChannels) :
            strand_{ std::move(executor) }, filename_{ filepath }, save_timer_{ strand_ }
        {
            channel_data_.reserve(expected_channels);
            channel_data_.max_load_factor(kChannelDataMaxLoadFactor);
        }

        ~ChannelStore();

        ChannelStore(const ChannelStore&) = delete;
        ChannelStore& operator=(const ChannelStore&) = delete;
        ChannelStore(ChannelStore&&) = delete;
        ChannelStore& operator=(ChannelStore&&) = delete;

        // Best effort: leaves map unchanged on parse errors.
        void load();

        // Debounced writeback. Schedules a save if data changed.
        void save() const noexcept;

        // --- thread-safe API -----------------------------------------------------

        // Insert if absent. Key is normalised to lowercase.
        void add_channel(std::string_view channel);

        // Erase if present. No throw on missing.
        void remove_channel(std::string_view channel) noexcept;

        // Case-insensitive via lowercase keys.
        [[nodiscard]] bool contains(std::string_view channel) const noexcept;

        // Returns a copy to avoid dangling if the map mutates later.
        [[nodiscard]] std::optional<std::string> get_alias(std::string_view channel) const;

        // Set or clear the alias for an existing channel.
        void set_alias(std::string_view channel, std::optional<std::string> alias);

        // Copy current channel names into out. Reuses out capacity.
        void channel_names(std::vector<std::string>& out) const;

    private:
        [[nodiscard]] toml::table build_table() const; // snapshot under shared lock
        void perform_save() const noexcept; // temp file then rename

        // Locale-free ASCII lowercasing. Intentional: Twitch names are ASCII.
        static std::string to_lower_ascii(std::string_view s)
        {
            std::string out;
            out.reserve(s.size());
            for (unsigned char c : s)
            {
                out.push_back((c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : char(c));
            }
            return out;
        }

        mutable std::shared_mutex data_mutex_;
        std::unordered_map<std::string, ChannelInfo> channel_data_; // lowercase keys

        boost::asio::strand<boost::asio::any_io_executor> strand_;
        const std::filesystem::path filename_;

        // Debounced writeback state.
        mutable std::atomic<bool> dirty_{ false };
        mutable std::atomic<bool> timer_scheduled_{ false };
        mutable boost::asio::steady_timer save_timer_;
    };

} // namespace app
