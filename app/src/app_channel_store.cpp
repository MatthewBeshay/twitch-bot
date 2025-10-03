/*
Module: app_channel_store.cpp

Purpose:
- Persist tiny per channel app state in a simple TOML file.

Why:
- Normalise channel keys to lower case so lookups match Twitch semantics
  and remain stable regardless of user input.
- Loading is best effort so a corrupt or missing file does not stop the app.
- Saving writes to a temp file then renames to avoid partial writes on crash.

Notes:
- Lower casing is ASCII only by design to avoid locale surprises.
- The on disk shape is:
    [channels]
    <channel> = "<value>"
*/

// C++ Standard Library
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

// Toml++
#include <toml++/toml.hpp>

// App
#include <app/app_channel_store.hpp>

namespace app
{

    std::string AppChannelStore::to_lower_ascii(std::string_view s)
    {
        // ASCII only to keep behaviour predictable and fast.
        std::string out;
        out.reserve(s.size());
        for (unsigned char c : s)
        {
            out.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : static_cast<char>(c));
        }
        return out;
    }

    void AppChannelStore::load()
    {
        per_channel_.clear();

        if (!std::filesystem::exists(path_))
        {
            return;
        }

        toml::table root;
        try
        {
            root = toml::parse_file(path_.string());
        }
        catch (...)
        {
            return; // best effort: ignore parse or IO errors
        }

        if (auto* chs = root.get_as<toml::table>("channels"))
        {
            for (auto&& [chan_key, chan_node] : *chs)
            {
                if (auto sval = chan_node.value<std::string>(); sval.has_value())
                {
                    // Keys are normalised once at load to keep the map consistent.
                    per_channel_.emplace(to_lower_ascii(chan_key.str()), std::move(*sval));
                }
            }
        }
    }

    void AppChannelStore::save() const noexcept
    {
        toml::table chs;
        for (const auto& [chan, value] : per_channel_)
        {
            chs.insert_or_assign(chan, value);
        }

        toml::table root;
        root.insert_or_assign("channels", std::move(chs));

        // Ensure the directory exists before writing.
        std::error_code ec;
        if (!path_.parent_path().empty())
        {
            std::filesystem::create_directories(path_.parent_path(), ec);
        }

        // Atomic write: write to tmp then rename into place.
        const auto tmp = path_.string() + ".tmp";
        {
            std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
            if (!ofs)
            {
                return; // best effort
            }
            ofs << root;
            if (!ofs)
            {
                return; // best effort
            }
        }
        std::filesystem::rename(tmp, path_, ec);
        if (ec)
        {
            // Clean up tmp on failure. Ignore errors here as well.
            std::filesystem::remove(tmp, ec);
        }
    }

} // namespace app
