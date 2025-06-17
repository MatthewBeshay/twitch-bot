// C++ Standard Library
#include <fstream>
#include <future>
#include <iostream>
#include <system_error>

// Project
#include "channel_store.hpp"

namespace {
// Delay before actually writing out the file
inline constexpr std::chrono::seconds kSaveDelay{5};

}

namespace twitch_bot {

// Ensure all pending handlers finish
ChannelStore::~ChannelStore()
{

    try {
        std::promise<void> promise_obj;
        auto future_obj = promise_obj.get_future();
        boost::asio::post(strand_, [promise_local = std::move(promise_obj)]() mutable {
            promise_local.set_value();
        });
        future_obj.wait();

    } catch (const std::exception &ex) {
        std::cerr << "[ChannelStore::~ChannelStore] exception: " << ex.what() << '\n';

    } catch (...) {
        std::cerr << "[ChannelStore::~ChannelStore] unknown exception\n";
    }
}

void ChannelStore::load()
{
    if (!std::filesystem::exists(filename_)) {
        return;
    }

    toml::table tbl;
    try {
        tbl = toml::parse_file(filename_.string());
    } catch (const toml::parse_error &e) {
        std::cerr << "[ChannelStore] parse error: " << e << '\n';
        return;
    } catch (const std::filesystem::filesystem_error &e) {
        std::cerr << "[ChannelStore] fs error: " << e.what() << '\n';
        return;
    }

    std::lock_guard<std::shared_mutex> guard{data_mutex_};
    channel_data_.clear();
    channel_data_.reserve(tbl.size());

    for (auto const &[key, node] : tbl) {
        if (auto *table_ptr = node.as_table()) {
            ChannelInfo info;
            if (auto *alias_node = table_ptr->get("alias");
                alias_node != nullptr && alias_node->is_string())

            {
                info.alias = alias_node->value<std::string>();
                channel_data_.emplace(std::piecewise_construct,
                                      std::forward_as_tuple(key),
                                      std::forward_as_tuple(std::move(info)));
            }
        }
    }
}

void ChannelStore::save() const noexcept
{
    dirty_.store(true, std::memory_order_relaxed);

    bool expected = false;
    if (timer_scheduled_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        save_timer_.expires_after(kSaveDelay);
        save_timer_.async_wait([this](auto const &err) {
            timer_scheduled_.store(false, std::memory_order_relaxed);
            if (!err && dirty_.exchange(false, std::memory_order_relaxed)) {
                perform_save();
            }
        });
    }
}

void ChannelStore::perform_save() const noexcept
{
    toml::table tbl = build_table();

    // Write to temp file and rename
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

    std::error_code error_code;
    std::filesystem::rename(tmp, filename_, error_code);

    if (error_code) {
        std::cerr << "[ChannelStore] rename failed: " << error_code.message() << '\n';
        std::filesystem::remove(tmp, error_code);
    }
}

toml::table ChannelStore::build_table() const
{
    toml::table tbl;
    std::shared_lock<std::shared_mutex> guard{data_mutex_};

    for (auto const &[key, info] : channel_data_) {
        toml::table entry_table;
        if (info.alias) {
            entry_table.insert("alias", *info.alias);
        }
        tbl.insert(key, std::move(entry_table));
    }

    return tbl;
}

} // namespace twitch_bot
