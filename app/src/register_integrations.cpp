// C++ Standard Library
#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>

// Boost.Asio
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>

// App
#include <app/register_integrations.hpp>

namespace app
{
    namespace
    {

        // "chat" -> "chat", "#chat" -> "chat", ASCII lower
        std::string canonical_channel(std::string_view s)
        {
            if (!s.empty() && s.front() == '#')
                s.remove_prefix(1);
            std::string out;
            out.reserve(s.size());
            for (unsigned char c : s)
                out.push_back((c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : char(c));
            return out;
        }

        // Keep last 4 chars, mask the rest.
        std::string mask_tail(std::string_view s)
        {
            if (s.empty())
                return {};
            const std::size_t keep = std::min<std::size_t>(4, s.size());
            std::string out;
            out.reserve(s.size());
            out.append(s.size() - keep, '*');
            out.append(s.substr(s.size() - keep));
            return out;
        }

    } // namespace

    void register_integrations(twitch_bot::TwitchBot& bot,
                               const app::Integrations& integrations,
                               app::AppChannelStore& /*store*/)
    {
        auto& dispatcher_ = bot.dispatcher();
        auto& helix_ = bot.helix();
    }

} // namespace app
