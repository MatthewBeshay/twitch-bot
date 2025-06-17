// C++ Standard Library
#include <fstream>
#include <future>
#include <iostream>
#include <system_error>

// Project
#include "channel_store.hpp"

namespace {

// Write-back debounce interval.
inline constexpr std::chrono::seconds kSaveDelay{5};

} // unnamed namespace

namespace twitch_bot {

// Wait until every handler queued on strand_ completes.
ChannelStore::~ChannelStore()
{
    try {
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
    toml::table tbl = build_table();

    // Write to temporary file then atomically rename.
    const auto tmp = filename_.string() + ".tmp";
    {
        std::ofstream out{tmp, std::ios::trunc};
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

} // namespace twitch_bot
