#pragma once

#include "message_parser.hpp"
#include "utils/transparent_string.hpp"
#include "utils/attributes.hpp"

#include <string>
#include <string_view>
#include <functional>
#include <vector>
#include <unordered_map>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

namespace twitch_bot {

using ChatListener   = std::function<void(std::string_view /*channel*/,
                                          std::string_view /*user*/,
                                          std::string_view /*text*/)>;

using CommandHandler = std::function<boost::asio::awaitable<void>(
                                          std::string_view /*channel*/,
                                          std::string_view /*user*/,
                                          std::string_view /*args*/,
                                          bool              /*isModerator*/)>;

class CommandDispatcher {
public:
    explicit CommandDispatcher(boost::asio::any_io_executor executor) noexcept;

    void registerCommand(std::string_view command,
                         CommandHandler  handler) noexcept;

    void registerChatListener(ChatListener listener) noexcept;

    /// Dispatch a parsed IRC message. Returns immediately.
    void dispatch(const IrcMessage& message) noexcept;

private:
    static TB_FORCE_INLINE std::string_view
    normalizeChannel(std::string_view raw) noexcept
    {
        // strip leading '#'
        return (!raw.empty() && raw.front() == '#')
             ? raw.substr(1)
             : raw;
    }

    static TB_FORCE_INLINE std::string_view
    extractUser(std::string_view prefix) noexcept
    {
        // take the portion before '!'
        auto pos = prefix.find('!');
        return (pos != std::string_view::npos)
             ? prefix.substr(0, pos)
             : prefix;
    }

    static TB_FORCE_INLINE void
    splitCommand(std::string_view  text,
                 std::string_view& outCmd,
                 std::string_view& outArgs) noexcept
    {
        // text must start with '!'
        if (text.size() > 1 && text.front() == '!') {
            auto pos = text.find(' ');
            if (pos == std::string_view::npos) {
                outCmd  = text.substr(1);
                outArgs = {};
            } else {
                outCmd  = text.substr(1, pos - 1);
                outArgs = text.substr(pos + 1);
            }
        } else {
            outCmd  = {};
            outArgs = {};
        }
    }

    boost::asio::any_io_executor                                   executor_;
    std::unordered_map<std::string, CommandHandler,
                       TransparentStringHash, TransparentStringEq> commands_;
    std::vector<ChatListener>                                      chat_listeners_;
};

} // namespace twitch_bot
