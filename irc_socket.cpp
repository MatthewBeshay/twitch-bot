#include "irc_socket.hpp"
#include <boost/system/system_error.hpp>
#include <array>

IrcSocket::IrcSocket(asio::io_context& ioc,
                     ssl::context&     ssl_ctx) noexcept
  : ws_(asio::make_strand(ioc), ssl_ctx)
{}

void IrcSocket::connect(std::string_view host,
                        std::string_view port)
{
    error_code ec;

    // 1) DNS lookup
    tcp::resolver resolver(ws_.get_executor());
    auto endpoints = resolver.resolve(
        std::string(host), std::string(port), ec);
    if (ec) throw boost::system::system_error(ec);

    // 2) TCP connect
    auto& sock = ws_.next_layer().lowest_layer();
    asio::connect(sock, endpoints, ec);
    if (ec) throw boost::system::system_error(ec);

    // 3) SSL handshake
    ws_.next_layer().handshake(ssl::stream_base::client, ec);
    if (ec) throw boost::system::system_error(ec);

    // 4) WebSocket handshake
    ws_.handshake(std::string(host), "/");
}

std::string
IrcSocket::readLine(error_code& ec) noexcept
{
    buffer_.consume(buffer_.size());
    ws_.read(buffer_, ec);
    if (ec) return {};
    return beast::buffers_to_string(buffer_.data());
}

void IrcSocket::writeLine(std::string_view msg,
                          error_code&       ec) noexcept
{
    ws_.text(true);

    std::array<asio::const_buffer,2> bufs{{
        asio::buffer(msg),
        asio::buffer(kCrLf)
    }};

    ws_.write(bufs, ec);
}

void IrcSocket::close(error_code& ec) noexcept {
    ws_.close(ws::close_code::normal, ec);
}

awaitable<std::string> IrcSocket::readLineAsync() {
    beast::flat_buffer buf;
    std::size_t n = co_await ws_.async_read(buf, use_awaitable);
    co_return beast::buffers_to_string(buf.data());
}

awaitable<void> IrcSocket::writeLineAsync(std::string_view msg) {
    std::array<asio::const_buffer,2> bufs{{
        asio::buffer(msg),
        asio::buffer(kCrLf)
    }};
    co_await ws_.async_write(bufs, use_awaitable);
}
