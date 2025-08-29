// C++ Standard Library
#include <fstream>
#include <future>
#include <iostream>
#include <sstream>
#include <system_error>

// App
#include <app/channel_store.hpp>

namespace
{
    inline constexpr std::chrono::seconds kSaveDelay{ 5 };
} // namespace

namespace app
{

    ChannelStore::~ChannelStore()
    {
        // Drain strand work if destroyed from a different thread.
        try
        {
            if (strand_.running_in_this_thread())
            {
                return;
            }
            std::promise<void> done;
            auto fut = done.get_future();
            boost::asio::post(strand_, [p = std::move(done)]() mutable { p.set_value(); });
            fut.wait();
        }
        catch (const std::exception& ex)
        {
            std::cerr << "[ChannelStore::~ChannelStore] exception: " << ex.what() << '\n';
        }
        catch (...)
        {
            std::cerr << "[ChannelStore::~ChannelStore] unknown exception\n";
        }
    }

    void ChannelStore::load()
    {
        if (!std::filesystem::exists(filename_))
        {
            return;
        }

        toml::table tbl;
        try
        {
            tbl = toml::parse_file(filename_.string());
        }
        catch (const toml::parse_error& e)
        {
            std::cerr << "[ChannelStore] parse error: " << e << '\n';
            return;
        }
        catch (const std::filesystem::filesystem_error& e)
        {
            std::cerr << "[ChannelStore] fs error: " << e.what() << '\n';
            return;
        }

        std::lock_guard guard{ data_mutex_ };
        channel_data_.clear();
        channel_data_.reserve(tbl.size());

        for (const auto& [key, node] : tbl)
        {
            if (auto* t = node.as_table())
            {
                ChannelInfo info;
                if (auto* alias_node = t->get("alias"); alias_node && alias_node->is_string())
                {
                    info.alias = alias_node->value<std::string>();
                }

                // Normalize channel to lowercase on load.
                channel_data_.emplace(std::piecewise_construct,
                                      std::forward_as_tuple(to_lower_ascii(key.str())),
                                      std::forward_as_tuple(std::move(info)));
            }
        }
    }

    void ChannelStore::save() const noexcept
    {
        dirty_.store(true, std::memory_order_relaxed);

        bool expected = false;
        if (timer_scheduled_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        {
            save_timer_.expires_after(kSaveDelay);
            save_timer_.async_wait([this](const auto& err) {
                timer_scheduled_.store(false, std::memory_order_relaxed);
                if (!err && dirty_.exchange(false, std::memory_order_relaxed))
                {
                    perform_save();
                }
            });
        }
    }

    void ChannelStore::perform_save() const noexcept
    {
        toml::table tbl = build_table();

        const auto tmp = filename_.string() + ".tmp";
        {
            std::ofstream out{ tmp, std::ios::trunc | std::ios::binary };
            if (!out)
            {
                std::cerr << "[ChannelStore] cannot open " << tmp << '\n';
                return;
            }

            std::ostringstream oss;
            oss << tbl;
            const auto data = oss.str();
            out.write(data.data(), static_cast<std::streamsize>(data.size()));
            if (!out)
            {
                std::cerr << "[ChannelStore] write failed: " << tmp << '\n';
                return;
            }
        }

        std::error_code ec;
        std::filesystem::rename(tmp, filename_, ec);
        if (ec)
        {
            std::cerr << "[ChannelStore] rename failed: " << ec.message() << '\n';
            std::filesystem::remove(tmp, ec);
        }
    }

    toml::table ChannelStore::build_table() const
    {
        toml::table tbl;
        std::shared_lock guard{ data_mutex_ };

        for (const auto& [key, info] : channel_data_)
        {
            toml::table entry;
            if (info.alias)
            {
                entry.insert("alias", *info.alias);
            }
            tbl.insert(key, std::move(entry));
        }
        return tbl;
    }

    // ------------------ thread-safe API ------------------

    void ChannelStore::add_channel(std::string_view channel)
    {
        const std::string lc = to_lower_ascii(channel);
        std::lock_guard guard{ data_mutex_ };
        auto [_, inserted] = channel_data_.emplace(
            std::piecewise_construct, std::forward_as_tuple(lc), std::forward_as_tuple());
        if (inserted)
        {
            dirty_.store(true, std::memory_order_relaxed);
        }
    }

    void ChannelStore::remove_channel(std::string_view channel) noexcept
    {
        const std::string lc = to_lower_ascii(channel);
        std::lock_guard guard{ data_mutex_ };
        const auto n = channel_data_.erase(lc);
        if (n)
        {
            dirty_.store(true, std::memory_order_relaxed);
        }
    }

    bool ChannelStore::contains(std::string_view channel) const noexcept
    {
        const std::string lc = to_lower_ascii(channel);
        std::shared_lock guard{ data_mutex_ };
        return channel_data_.find(lc) != channel_data_.end();
    }

    std::optional<std::string> ChannelStore::get_alias(std::string_view channel) const
    {
        const std::string lc = to_lower_ascii(channel);
        std::shared_lock guard{ data_mutex_ };
        if (auto it = channel_data_.find(lc); it != channel_data_.end() && it->second.alias)
        {
            return *it->second.alias; // copy
        }
        return std::nullopt;
    }

    void ChannelStore::set_alias(std::string_view channel, std::optional<std::string> alias)
    {
        const std::string lc = to_lower_ascii(channel);
        std::lock_guard guard{ data_mutex_ };
        if (auto it = channel_data_.find(lc); it != channel_data_.end())
        {
            if (it->second.alias != alias)
            {
                it->second.alias = std::move(alias);
                dirty_.store(true, std::memory_order_relaxed);
            }
        }
    }

    void ChannelStore::channel_names(std::vector<std::string>& out) const
    {
        std::shared_lock guard{ data_mutex_ };
        out.clear();
        out.reserve(channel_data_.size());
        for (const auto& [name, _] : channel_data_)
        {
            out.push_back(name); // lowercase names
        }
    }

} // namespace app
