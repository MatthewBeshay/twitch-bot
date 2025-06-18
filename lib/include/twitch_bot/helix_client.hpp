#pragma once

// C++ Standard Library
#include <chrono>
#include <optional>
#include <string>
#include <string_view>

// 3rd-party
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/strand.hpp>

// Project
#include "http_client.hpp"
#include "utils/attributes.hpp"

namespace twitch_bot {

using json = glz::json_t;

struct StreamStatus {
    bool is_live{false};
    std::chrono::milliseconds start_time{0};
};

class HelixClient
{
public:
    HelixClient(boost::asio::any_io_executor executor,
                boost::asio::ssl::context& ssl_ctx,
                std::string_view client_id,
                std::string_view client_secret) noexcept;

    HelixClient(const HelixClient&) = delete;
    HelixClient& operator=(const HelixClient&) = delete;
    HelixClient(HelixClient&&) = default;
    HelixClient& operator=(HelixClient&&) = default;
    ~HelixClient() = default;

    auto ensure_valid_token() noexcept -> boost::asio::awaitable<void>;

    [[nodiscard]]
    auto validate_token() noexcept -> boost::asio::awaitable<bool>;

    auto request_new_token() noexcept -> boost::asio::awaitable<void>;

    auto get_stream_status(std::string_view channel_id) noexcept
        -> boost::asio::awaitable<std::optional<StreamStatus>>;

private:
    boost::asio::strand<boost::asio::any_io_executor> strand_;
    std::string token_;
    std::chrono::steady_clock::time_point token_expiry_;
    std::string token_body_;
    boost::asio::any_io_executor executor_;
    http_client::client http_client_;
    const std::string client_id_;
    const std::string client_secret_;
};

} // namespace twitch_bot
