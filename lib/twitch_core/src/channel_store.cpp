/*
Module Name:
- channel_store.cpp

Abstract:
- Persistence and concurrency mechanics for ChannelStore.

Why:
- Debounced writes avoid hammering the filesystem during bursts of updates.
- Destructor waits on the strand so no handlers outlive this object.
- Save is crash safe by writing to a temp file then renaming atomically.
*/

// C++ Standard Library
#include <fstream>
#include <future>
#include <iostream>
#include <sstream>
#include <system_error>

// Core
#include <tb/twitch/channel_store.hpp>

namespace {
// Debounce interval for write back. Large enough to batch edits, small enough to feel prompt.
inline constexpr std::chrono::seconds kSaveDelay{5};
} // unnamed namespace

namespace twitch_bot {

// Waits until every handler already queued on strand_ completes.
// Why: avoids destroying state while work is still pending on the same strand.
ChannelStore::~ChannelStore()
{
    try {
        // If already on the strand then no other handlers can run concurrently here.
        if (strand_.running_in_this_thread())
            return;

        std::promise<void> done;
        auto fut = done.get_future();
        boost::asio::post(strand_, [p = std::move(done)]() mutable { p.set_value(); });
        fut.wait();
    } catch (const std::exception& ex) {
        std::cerr << "[ChannelStore::~ChannelStore] exception: " << ex.what() << '\n';
    } catch (...) {
        std::cerr << "[ChannelStore::~ChannelStore] unknown exception\n";
    }
}

void ChannelStore::load()
{
    if (!std::filesystem::exists(filename_))
        return;

    toml::table tbl;
    try {
        tbl = toml::parse_file(filename_.string());
    } catch (const toml::parse_error& e) {
        std::cerr << "[ChannelStore] parse error: " << e << '\n';
        return;
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "[ChannelStore] fs error: " << e.what() << '\n';
        return;
    }

    std::lock_guard guard{data_mutex_};
    channel_data_.clear();
    channel_data_.reserve(tbl.size());

    for (const auto& [key, node] : tbl) {
        if (auto* tbl_ptr = node.as_table()) {
            ChannelInfo info;
            if (auto* alias_node = tbl_ptr->get("alias"); alias_node && alias_node->is_string())
                info.alias = alias_node->value<std::string>();

            channel_data_.emplace(std::piecewise_construct,
                                  std::forward_as_tuple(key),
                                  std::forward_as_tuple(std::move(info)));
        }
    }
}

void ChannelStore::save() const noexcept
{
    // Mark dirty and ensure exactly one timer is scheduled to coalesce bursts.
    dirty_.store(true, std::memory_order_relaxed);

    bool expected = false;
    if (timer_scheduled_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        save_timer_.expires_after(kSaveDelay);
        save_timer_.async_wait([this](const auto& err) {
            timer_scheduled_.store(false, std::memory_order_relaxed);
            if (!err && dirty_.exchange(false, std::memory_order_relaxed))
                perform_save();
        });
    }
}

void ChannelStore::perform_save() const noexcept
{
    // Build a snapshot under shared lock, then write without holding locks.
    toml::table tbl = build_table();

    // Write to a temp file then atomically rename so readers never see partial files.
    const auto tmp = filename_.string() + ".tmp";
    {
        std::ofstream out{tmp, std::ios::trunc | std::ios::binary};
        if (!out) {
            std::cerr << "[ChannelStore] cannot open " << tmp << '\n';
            return;
        }

        std::ostringstream oss;
        oss << tbl;
        const auto data = oss.str();
        out.write(data.data(), static_cast<std::streamsize>(data.size()));

        if (!out) {
            std::cerr << "[ChannelStore] write failed: " << tmp << '\n';
            return;
        }
    }

    std::error_code ec;
    std::filesystem::rename(tmp, filename_, ec);
    if (ec) {
        std::cerr << "[ChannelStore] rename failed: " << ec.message() << '\n';
        std::filesystem::remove(tmp, ec);
    }
}

toml::table ChannelStore::build_table() const
{
    toml::table tbl;
    std::shared_lock guard{data_mutex_};

    for (const auto& [key, info] : channel_data_) {
        toml::table entry;
        if (info.alias)
            entry.insert("alias", *info.alias);
        tbl.insert(key, std::move(entry));
    }
    return tbl;
}

// ------------------ thread safe API ------------------

void ChannelStore::add_channel(std::string_view channel)
{
    std::lock_guard guard{data_mutex_};
    channel_data_.emplace(
        std::piecewise_construct, std::forward_as_tuple(channel), std::forward_as_tuple());
    dirty_.store(true, std::memory_order_relaxed); // nudge debounced save
}

void ChannelStore::remove_channel(std::string_view channel) noexcept
{
    std::lock_guard guard{data_mutex_};
    channel_data_.erase(channel);
    dirty_.store(true, std::memory_order_relaxed); // nudge debounced save
}

bool ChannelStore::contains(std::string_view channel) const noexcept
{
    std::shared_lock guard{data_mutex_};
    return channel_data_.find(channel) != channel_data_.end();
}

std::optional<std::string> ChannelStore::get_alias(std::string_view channel) const
{
    std::shared_lock guard{data_mutex_};
    if (auto itr = channel_data_.find(channel); itr != channel_data_.end() && itr->second.alias) {
        return *itr->second.alias; // copy so callers can keep it after map mutations
    }
    return std::nullopt;
}

void ChannelStore::set_alias(std::string_view channel, std::optional<std::string> alias)
{
    std::lock_guard guard{data_mutex_};
    if (auto itr = channel_data_.find(channel); itr != channel_data_.end()) {
        itr->second.alias = std::move(alias);
        dirty_.store(true, std::memory_order_relaxed);
    }
}

void ChannelStore::channel_names(std::vector<std::string_view>& out) const
{
    std::shared_lock guard{data_mutex_};
    out.clear();
    out.reserve(channel_data_.size());
    for (const auto& [name, info] : channel_data_) {
        out.push_back(name);
    }
    // Note: returned views point into the map keys. They become dangling after a mutation.
    // Why: avoids allocations here. Callers should copy if they need long lived strings.
}

} // namespace twitch_bot
