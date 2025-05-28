#pragma once

#include <array>
#include <mutex>
#include <string>
#include <string_view>

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

namespace asio  = boost::asio;
namespace ssl   = asio::ssl;
namespace beast = boost::beast;
namespace ws    = beast::websocket;

using tcp         = asio::ip::tcp;
using error_code  = boost::system::error_code;
using asio::awaitable;
using asio::use_awaitable;

/// \brief Thread-safe IRC client over WebSocket+SSL.
/// \note Do not mix sync and async calls on the same socket concurrently.
class IrcSocket
{
public:
    /// \brief Construct with an I/O context and SSL context.
    explicit IrcSocket(asio::io_context& ioc,
                       ssl::context&     ssl_ctx) noexcept;

    IrcSocket(const IrcSocket&)            = delete;
    IrcSocket& operator=(const IrcSocket&) = delete;

    IrcSocket(IrcSocket&&) noexcept            = default;
    IrcSocket& operator=(IrcSocket&&) noexcept = default;
    ~IrcSocket() noexcept                     = default;

    /// \brief Blocking connect: DNS -> TCP -> SSL -> WebSocket.
    /// \throws boost::system::system_error on error.
    void connect(std::string_view host,
                 std::string_view port);

    /// \brief Blocking read of one IRC message (one WebSocket text frame).
    /// \returns The text of the frame, or empty string on error.
    std::string readLine(error_code& ec) noexcept;

    /// \brief Blocking, thread-safe write of one IRC message (appends CRLF).
    void writeLine(std::string_view msg,
                   error_code&       ec) noexcept;

    /// \brief Cleanly close WebSocket and SSL layers.
    void close(error_code& ec) noexcept;

    /// \brief Asynchronously read one text frame.
    /// \returns The text of the frame.
    awaitable<std::string> readLineAsync();

    /// \brief Asynchronously write one text frame (appends CRLF).
    awaitable<void> writeLineAsync(std::string_view msg);

private:
    ws::stream<ssl::stream<tcp::socket>> ws_;
    beast::flat_buffer                   buffer_;  ///< for sync reads
    std::mutex                           mtx_;     ///< protects buffer_

    static inline constexpr std::string_view CRLF = "\r\n";
};
