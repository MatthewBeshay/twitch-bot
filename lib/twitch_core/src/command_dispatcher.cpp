// C++ Standard Library
#include <iostream>

// Boost.Asio
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>

// Project
#include <tb/twitch/command_dispatcher.hpp>

namespace twitch_bot {

CommandDispatcher::CommandDispatcher(boost::asio::any_io_executor executor)
    : executor_(std::move(executor))
{
    commands_.reserve(16);
    chat_listeners_.reserve(4);
}

void CommandDispatcher::register_command(std::string_view command, command_handler_t handler)
{
    // Insert-or-ignore duplicate command registration; adjust policy if you prefer overwrite.
    (void)commands_.try_emplace(std::string{command}, std::move(handler));
}

void CommandDispatcher::register_chat_listener(chat_listener_t listener)
{
    chat_listeners_.push_back(std::move(listener));
}

// Run the handler and surface errors on stderr.
template <typename Handler>
boost::asio::awaitable<void> invoke_command(Handler handler, IrcMessage msg)
{
    try {
        co_await handler(std::move(msg));
    } catch (const std::exception& e) {
        std::cerr << "[dispatcher] '" << msg.command << "' threw: " << e.what() << '\n';
    } catch (...) {
        std::cerr << "[dispatcher] '" << msg.command << "' threw: <unknown exception>\n";
    }
}

// Route a single line, either as a command or a plain chat message.
void CommandDispatcher::route_text(std::string_view channel,
                                   std::string_view user,
                                   std::string_view text,
                                   std::string_view raw_tags,
                                   bool is_moderator,
                                   bool is_broadcaster)
{
    // '!cmd ...'
    if (!text.empty() && text.front() == '!') {
        std::string_view cmd_name;
        std::string_view args;
        split_command(text, cmd_name, args);
        if (auto it = commands_.find(cmd_name); it != commands_.end()) {
            IrcMessage cmd_msg{};
            cmd_msg.command = cmd_name;
            cmd_msg.params[0] = channel;
            cmd_msg.param_count = 1;
            cmd_msg.prefix = user;
            cmd_msg.trailing = args;
            cmd_msg.raw_tags = raw_tags; // pass-through tags
            cmd_msg.is_moderator = is_moderator ? 1 : 0; // role bits
            cmd_msg.is_broadcaster = is_broadcaster ? 1 : 0;

            // Copy the handler into the coroutine to decouple from map lifetime during await.
            boost::asio::co_spawn(
                executor_, invoke_command(it->second, std::move(cmd_msg)), boost::asio::detached);
            return;
        }
    }

    // Plain chat line - still notify listeners
    for (auto& listener : chat_listeners_)
        listener(channel, user, text);
}

void CommandDispatcher::dispatch_text(std::string_view channel,
                                      std::string_view user,
                                      std::string_view text)
{
    // No tags available in this entry point
    route_text(channel, user, text);
}

void CommandDispatcher::dispatch(IrcMessage msg)
{
    // Only chat lines
    if (msg.command != "PRIVMSG" || msg.param_count < 1)
        return;

    std::string_view channel = normalize_channel(msg.params[0]);
    std::string_view user = extract_user(msg.prefix);
    std::string_view text = msg.trailing;

    // Pass through tags and role bits from the source PRIVMSG
    route_text(channel, user, text, msg.raw_tags, msg.is_moderator != 0, msg.is_broadcaster != 0);
}

} // namespace twitch_bot
