// C++ Standard Library
#include <fstream>
#include <future>
#include <iostream>
#include <system_error>

// Project
#include "channel_store.hpp"

namespace twitch_bot {

// Ensure all pending handlers finish
ChannelStore::~ChannelStore()
{
    std::promise<void> p;
    auto f = p.get_future();
    boost::asio::post(strand_, [pr = std::move(p)]() mutable { pr.set_value(); });
    f.wait();
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

    std::lock_guard<std::shared_mutex> guard{data_mutex_};
    channel_data_.clear();
    channel_data_.reserve(tbl.size());

    for (auto& [key, node] : tbl) {
        if (auto table = node.as_table()) {
            ChannelInfo info;
            if (auto n = table->get("alias"); n && n->is_string()) {
                info.alias = n->value<std::string>();
            }
            if (auto n2 = table->get("faceit_nick"); n2 && n2->is_string()) {
                info.faceit_nick = n2->value<std::string>();
            }
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
        save_timer_.expires_after(std::chrono::seconds{5});
        save_timer_.async_wait([this](auto const& ec) {
            timer_scheduled_.store(false, std::memory_order_relaxed);
            if (!ec && dirty_.exchange(false, std::memory_order_relaxed)) {
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
    std::shared_lock<std::shared_mutex> guard{data_mutex_};

    for (auto& [key, info] : channel_data_) {
        toml::table entry;
        if (info.alias) {
            entry.insert("alias", *info.alias);
        }
        if (info.faceit_nick) {
            entry.insert("faceit_nick", *info.faceit_nick);
        }
        if (info.faceit_id) {
            entry.insert("faceit_id", *info.faceit_id);
        }
        tbl.insert(key, std::move(entry));
    }
    return tbl;
}

} // namespace twitch_bot
