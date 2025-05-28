#include "irc_socket.hpp"

#include <openssl/ssl.h>
#include <boost/asio/connect.hpp>
#include <boost/system/system_error.hpp>

IrcSocket::IrcSocket(asio::io_context& ioc,
                     ssl::context&     ssl_ctx) noexcept
    : ws_(asio::make_strand(ioc), ssl_ctx)
{}

//------------------------------------------------------------------------------
// Blocking connect: DNS -> TCP -> SSL -> WebSocket
void IrcSocket::connect(std::string_view host,
                        std::string_view port)
{
    error_code ec;

    // 1) Resolve host and port
    tcp::resolver resolver(ws_.get_executor());
    auto endpoints = resolver.resolve(std::string(host),
                                      std::string(port),
                                      ec);
    if (ec) throw boost::system::system_error(ec);

    // 2) TCP connect
    auto& sock = beast::get_lowest_layer(ws_.next_layer());
    asio::connect(sock, endpoints, ec);
    if (ec) throw boost::system::system_error(ec);

    // 3) SSL handshake with SNI
    if (!SSL_set_tlsext_host_name(
            ws_.next_layer().native_handle(),
            host.data()))
    {
        throw boost::system::system_error{
            static_cast<int>(::ERR_get_error()),
            asio::error::get_ssl_category()};
    }
    ws_.next_layer().handshake(ssl::stream_base::client, ec);
    if (ec) throw boost::system::system_error(ec);

    // 4) WebSocket handshake (Host header includes port)
    std::string host_header = std::string(host) + ":" + std::string(port);
    ws_.handshake(host_header, "/", ec);
    if (ec) throw boost::system::system_error(ec);
}

//------------------------------------------------------------------------------
// Blocking read of one text frame
std::string IrcSocket::readLine(error_code& ec) noexcept
{
    std::lock_guard lock(mtx_);
    buffer_.consume(buffer_.size());
    ws_.read(buffer_, ec);
    if (ec) return {};
    return beast::buffers_to_string(buffer_.data());
}

//------------------------------------------------------------------------------
// Blocking write of one text frame (+CRLF)
void IrcSocket::writeLine(std::string_view msg,
                          error_code&       ec) noexcept
{
    ws_.text(true);
    std::array<asio::const_buffer, 2> bufs{{
        asio::buffer(msg),
        asio::buffer(CRLF)
    }};
    ws_.write(bufs, ec);
}

//------------------------------------------------------------------------------
// Clean shutdown: WebSocket close, then SSL shutdown
void IrcSocket::close(error_code& ec) noexcept
{
    // WebSocket close
    ws_.close(ws::close_code::normal, ec);
    // SSL shutdown (ignore truncated errors)
    error_code ec2;
    ws_.next_layer().shutdown(ec2);
    if (ec2 && ec2 != ssl::error::stream_truncated)
        ec = ec2;
}

//------------------------------------------------------------------------------
// Async read one text frame
awaitable<std::string> IrcSocket::readLineAsync()
{
    beast::flat_buffer buf;
    co_await ws_.async_read(buf, use_awaitable);
    co_return beast::buffers_to_string(buf.data());
}

//------------------------------------------------------------------------------
// Async write one text frame (+CRLF)
awaitable<void> IrcSocket::writeLineAsync(std::string_view msg)
{
    ws_.text(true);
    std::array<asio::const_buffer, 2> bufs{{
        asio::buffer(msg),
        asio::buffer(CRLF)
    }};
    co_await ws_.async_write(bufs, use_awaitable);
}
