#pragma once

// C++ Standard Library
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

// Boost.Asio
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/strand.hpp>

// Glaze
#include <glaze/json.hpp>

// Project
#include <tb/net/http/http_client.hpp>
#include <tb/utils/attributes.hpp>

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
                std::string_view client_secret,
                std::string_view refresh_token);

    HelixClient(const HelixClient&) = delete;
    HelixClient& operator=(const HelixClient&) = delete;
    HelixClient(HelixClient&&) = default;
    HelixClient& operator=(HelixClient&&) = default;
    ~HelixClient();

    // Ensure we hold a valid access token; refresh with the stored refresh_token if needed.
    auto ensure_valid_token() -> boost::asio::awaitable<void>;

    // Validate the current token via /oauth2/validate and update expiry if valid.
    [[nodiscard]] auto validate_token() -> boost::asio::awaitable<bool>;

    // Refresh a user access token using the stored refresh token.
    auto refresh_token() -> boost::asio::awaitable<void>;

    // Return stream status for the given channel_id.
    auto get_stream_status(std::string_view channel_id)
        -> boost::asio::awaitable<std::optional<StreamStatus>>;

    [[nodiscard]] auto current_token() const noexcept -> const std::string&
    {
        return token_;
    }

    using AccessTokenPersistor = std::function<void(std::string_view)>;
    void set_access_token_persistor(AccessTokenPersistor cb) noexcept
    {
        persist_access_token_ = std::move(cb);
    }

private:
    boost::asio::strand<boost::asio::any_io_executor> strand_;

    std::string token_;
    std::chrono::steady_clock::time_point token_expiry_{};
    AccessTokenPersistor persist_access_token_{};
    const std::string client_id_;
    const std::string client_secret_;
    std::string refresh_token_value_;

    boost::asio::any_io_executor executor_;
    std::unique_ptr<http_client::client> http_client_;

    auto fetch_token(std::string body) -> boost::asio::awaitable<void>;
    [[nodiscard]] auto build_refresh_token_request_body() const -> std::string;
};

} // namespace twitch_bot
