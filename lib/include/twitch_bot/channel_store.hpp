#pragma once

#include "utils/transparent_string.hpp"
#include "utils/attributes.hpp"

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

struct ChannelInfo
{
    std::optional<std::string> alias;
};

class ChannelStore
{
public:
    explicit ChannelStore(boost::asio::any_io_executor executor,
                          const std::filesystem::path&   filepath        = "channels.toml",
                          std::size_t                    expected_channels = 256)
      : strand_{std::move(executor)}
      , filename_{filepath}
      , save_timer_{strand_}
    {
        channel_data_.reserve(expected_channels);
        channel_data_.max_load_factor(0.5f);
    }

    ~ChannelStore();

    ChannelStore(const ChannelStore&)            = delete;
    ChannelStore& operator=(const ChannelStore&) = delete;

    // Load existing file, if any
    void load();

    // Debounced async save
    void save() const noexcept;

    // Thread-safe modifiers and observers
    TB_FORCE_INLINE void
    add_channel(std::string_view channel) noexcept
    {
        std::lock_guard<std::shared_mutex> guard{data_mutex_};
        channel_data_.emplace(
            std::piecewise_construct,
            std::forward_as_tuple(channel),
            std::forward_as_tuple()
        );
    }

    TB_FORCE_INLINE void
    remove_channel(std::string_view channel) noexcept
    {
        std::lock_guard<std::shared_mutex> guard{data_mutex_};
        channel_data_.erase(channel);
    }

    TB_FORCE_INLINE bool
    contains(std::string_view channel) const noexcept
    {
        std::shared_lock<std::shared_mutex> guard{data_mutex_};
        return channel_data_.find(channel) != channel_data_.end();
    }

    TB_FORCE_INLINE std::optional<std::string_view>
    get_alias(std::string_view channel) const noexcept
    {
        std::shared_lock<std::shared_mutex> guard{data_mutex_};
        if (auto it = channel_data_.find(channel);
            it != channel_data_.end() && it->second.alias)
        {
            const auto& s = *it->second.alias;
            return std::string_view{s.data(), s.size()};
        }
        return std::nullopt;
    }

    TB_FORCE_INLINE void
    set_alias(std::string_view channel, std::optional<std::string> alias) noexcept
    {
        std::lock_guard<std::shared_mutex> guard{data_mutex_};
        if (auto it = channel_data_.find(channel);
            it != channel_data_.end())
        {
            it->second.alias = std::move(alias);
        }
    }

    // Populate caller-supplied vector to reuse capacity
    void channel_names(std::vector<std::string_view>& out) const noexcept
    {
        std::shared_lock<std::shared_mutex> guard{data_mutex_};
        out.clear();
        out.reserve(channel_data_.size());
        for (auto& [key, info] : channel_data_)
            out.push_back(key);
    }

private:
    toml::table build_table() const;
    void perform_save() const noexcept;

    mutable std::shared_mutex                                 data_mutex_;
    std::unordered_map<std::string, ChannelInfo,
                       TransparentStringHash,
                       TransparentStringEq>                    channel_data_;
    boost::asio::strand<boost::asio::any_io_executor>         strand_;
    const std::filesystem::path                               filename_;
    mutable std::atomic<bool>                                 dirty_{false};
    mutable std::atomic<bool>                                 timer_scheduled_{false};
    mutable boost::asio::steady_timer                         save_timer_;
};

} // namespace twitch_bot
