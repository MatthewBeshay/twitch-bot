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

// 3rd-party
#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <toml++/toml.hpp>

// Project
#include "utils/attributes.hpp"
#include "utils/transparent_string.hpp"

namespace twitch_bot {

/// Default bucket expectation for the channel map.
inline constexpr std::size_t kDefaultExpectedChannels = 256;

/// Load-factor threshold that triggers rehashing.
inline constexpr float kChannelDataMaxLoadFactor = 0.5F;

/// Per-channel metadata.
struct ChannelInfo {
    std::optional<std::string> alias; ///< user-friendly name
    std::optional<std::string> faceit_nick;
    std::optional<std::string> faceit_id;
};

/// Load, cache and persist a set of Twitch channels and their aliases.
/// All public operations are safe to call concurrently.
class ChannelStore
{
public:
    /// Construct the store.
    /// Channels are read from \p filepath on first \ref load().
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

    /// Load channel data from disk.
    void load();

    /// Persist channel data if modified.
    void save() const noexcept;

    // --- thread-safe modifiers and queries -------------------------------------

    TB_FORCE_INLINE void add_channel(std::string_view channel) noexcept
    {
        std::lock_guard guard{data_mutex_};
        channel_data_.emplace(
            std::piecewise_construct, std::forward_as_tuple(channel), std::forward_as_tuple());
        dirty_ = true;
    }

    TB_FORCE_INLINE void remove_channel(std::string_view channel) noexcept
    {
        std::lock_guard guard{data_mutex_};
        channel_data_.erase(channel);
        dirty_ = true;
    }

    TB_FORCE_INLINE bool contains(std::string_view channel) const noexcept
    {
        std::shared_lock guard{data_mutex_};
        return channel_data_.find(channel) != channel_data_.end();
    }

    /// Alias for \p channel if one exists.
    TB_FORCE_INLINE std::optional<std::string_view>
    get_alias(std::string_view channel) const noexcept
    {
        std::shared_lock guard{data_mutex_};
        if (auto itr = channel_data_.find(channel);
            itr != channel_data_.end() && itr->second.alias) {
            const auto& a = *itr->second.alias;
            return std::string_view{a.data(), a.size()};
        }
        return std::nullopt;
    }

    TB_FORCE_INLINE void set_alias(std::string_view channel,
                                   std::optional<std::string> alias) noexcept
    {
        std::lock_guard guard{data_mutex_};
        if (auto itr = channel_data_.find(channel); itr != channel_data_.end()) {
            itr->second.alias = std::move(alias);
            dirty_ = true;
        }
    }

    TB_FORCE_INLINE std::optional<std::string_view>
    get_faceit_nick(std::string_view channel) const noexcept
    {
        std::shared_lock<std::shared_mutex> guard{data_mutex_};
        if (auto it = channel_data_.find(channel);
            it != channel_data_.end() && it->second.faceit_nick) {
            const auto &s = *it->second.faceit_nick;
            return std::string_view{s.data(), s.size()};
        }
        return std::nullopt;
    }

    TB_FORCE_INLINE void set_faceit_nick(std::string_view channel,
                                         std::optional<std::string> nick) noexcept
    {
        std::lock_guard<std::shared_mutex> guard{data_mutex_};
        if (auto it = channel_data_.find(channel); it != channel_data_.end()) {
            it->second.faceit_nick = std::move(nick);
        }
    }

    TB_FORCE_INLINE std::optional<std::string_view>
    get_faceit_nick(std::string_view channel) const noexcept
    {
        std::shared_lock<std::shared_mutex> guard{data_mutex_};
        if (auto it = channel_data_.find(channel);
            it != channel_data_.end() && it->second.faceit_nick) {
            const auto &s = *it->second.faceit_nick;
            return std::string_view{s.data(), s.size()};
        }
        return std::nullopt;
    }

    TB_FORCE_INLINE void set_faceit_nick(std::string_view channel,
                                         std::optional<std::string> nick) noexcept
    {
        std::lock_guard<std::shared_mutex> guard{data_mutex_};
        if (auto it = channel_data_.find(channel); it != channel_data_.end()) {
            it->second.faceit_nick = std::move(nick);
        }
    }

    TB_FORCE_INLINE std::optional<std::string_view>
    get_faceit_id(std::string_view channel) const noexcept
    {
        std::shared_lock<std::shared_mutex> guard{data_mutex_};
        if (auto it = channel_data_.find(channel);
            it != channel_data_.end() && it->second.faceit_id) {
            auto &s = *it->second.faceit_id;
            return std::string_view{s.data(), s.size()};
        }
        return std::nullopt;
    }

    TB_FORCE_INLINE void set_faceit_id(std::string_view channel,
                                       std::optional<std::string> id) noexcept
    {
        std::lock_guard<std::shared_mutex> guard{data_mutex_};
        if (auto it = channel_data_.find(channel); it != channel_data_.end()) {
            it->second.faceit_id = std::move(id);
            save();
        }
    }

    TB_FORCE_INLINE std::optional<std::string_view>
    get_faceit_id(std::string_view channel) const noexcept
    {
        std::shared_lock<std::shared_mutex> guard{data_mutex_};
        if (auto it = channel_data_.find(channel);
            it != channel_data_.end() && it->second.faceit_id) {
            auto &s = *it->second.faceit_id;
            return std::string_view{s.data(), s.size()};
        }
        return std::nullopt;
    }

    TB_FORCE_INLINE void set_faceit_id(std::string_view channel,
                                       std::optional<std::string> id) noexcept
    {
        std::lock_guard<std::shared_mutex> guard{data_mutex_};
        if (auto it = channel_data_.find(channel); it != channel_data_.end()) {
            it->second.faceit_id = std::move(id);
            save();
        }
    }

    /// Copy the current channel names into \p out (capacity reused).
    void channel_names(std::vector<std::string_view>& out) const noexcept
    {
        std::shared_lock guard{data_mutex_};
        out.clear();
        out.reserve(channel_data_.size());
        for (auto& [name, info] : channel_data_)
            out.push_back(name);
    }

private:
    toml::table build_table() const;
    void perform_save() const noexcept;

    mutable std::shared_mutex data_mutex_;
    std::unordered_map<std::string, ChannelInfo, TransparentStringHash, TransparentStringEq>
        channel_data_;

    boost::asio::strand<boost::asio::any_io_executor> strand_;
    const std::filesystem::path filename_;

    mutable std::atomic<bool> dirty_{false};
    mutable std::atomic<bool> timer_scheduled_{false};
    mutable boost::asio::steady_timer save_timer_;
};

} // namespace twitch_bot
