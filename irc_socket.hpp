#pragma once

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <array>
#include <string>
#include <string_view>

namespace asio  = boost::asio;
namespace ssl   = asio::ssl;
namespace beast = boost::beast;
namespace ws    = beast::websocket;
using tcp       = asio::ip::tcp;

using asio::awaitable;
using asio::use_awaitable;
using error_code = boost::system::error_code;

/// @brief Thread-safe IRC client over WebSocket+SSL.
class IrcSocket {
public:
    explicit IrcSocket(asio::io_context& ioc,
                       ssl::context&     ssl_ctx) noexcept;

    IrcSocket(const IrcSocket&)            = delete;
    IrcSocket& operator=(const IrcSocket&) = delete;

    IrcSocket(IrcSocket&&) noexcept            = default;
    IrcSocket& operator=(IrcSocket&&) noexcept = default;
    ~IrcSocket() noexcept                     = default;

    /// Synchronous connect (DNS → TCP → SSL → WebSocket).
    /// @throws boost::system::system_error
    void connect(std::string_view host,
                 std::string_view port);

    /// Blocking read of one IRC message (one WebSocket text frame).
    [[nodiscard]] std::string
    readLine(error_code& ec) noexcept;

    /// Thread-safe sync write of one IRC message (CRLF appended).
    void writeLine(std::string_view msg,
                   error_code&       ec) noexcept;

    /// Cleanly close the WebSocket.
    void close(error_code& ec) noexcept;

    //
    // Coroutine-based APIs:
    //
    awaitable<std::string> readLineAsync();
    awaitable<void>        writeLineAsync(std::string_view msg);

private:
    ws::stream<ssl::stream<tcp::socket>> ws_;
    beast::flat_buffer                   buffer_;

    static inline constexpr std::string_view kCrLf = "\r\n";
};
