#include "http_client.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/version.hpp>
#include <openssl/ssl.h>
#include <stdexcept>

namespace http_client {
namespace {

    namespace asio  = boost::asio;
    namespace beast = boost::beast;
    namespace http  = beast::http;
    using tcp       = asio::ip::tcp;

    beast::ssl_stream<beast::tcp_stream>
    make_tls_stream(std::string_view host,
                    std::string_view port,
                    asio::io_context& ioc,
                    asio::ssl::context& ssl_ctx)
    {
        tcp::resolver resolver{ ioc };
        auto const endpoints = resolver.resolve(host, port);
        beast::tcp_stream tcp_stream{ ioc };
        tcp_stream.connect(endpoints);

        beast::ssl_stream<beast::tcp_stream> tls_stream{
            std::move(tcp_stream), ssl_ctx
        };
        if (!SSL_set_tlsext_host_name(
              tls_stream.native_handle(),
              std::string(host).c_str()))
        {
            throw std::runtime_error("SNI setup failed for host: " + std::string(host));
        }
        tls_stream.handshake(asio::ssl::stream_base::client);
        return tls_stream;
    }

    std::string read_response(beast::ssl_stream<beast::tcp_stream>& stream)
    {
        beast::flat_buffer buffer;
        http::response<http::string_body> res;
        http::read(stream, buffer, res);

        // Graceful TLS shutdown — ignore any errors
        {
            beast::error_code ec;
            stream.shutdown(ec);
            (void)ec;
        }

        if (res.result() != http::status::ok) {
            throw std::runtime_error(
                "HTTP request failed: status=" +
                std::to_string(res.result_int()) +
                " body=" + res.body()
            );
        }

        return res.body();
    }

    std::string
    sync_request(http::verb                         method,
                 std::string_view                  host,
                 std::string_view                  port,
                 std::string_view                  target,
                 std::string_view                  body,
                 std::map<std::string, std::string> const& headers,
                 asio::io_context&                 ioc,
                 asio::ssl::context&               ssl_ctx)
    {
        auto stream = make_tls_stream(host, port, ioc, ssl_ctx);

        if (method == http::verb::get) {
            http::request<http::empty_body> req{ method, target, 11 };
            req.set(http::field::host, host);
            req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
            for (auto const& h : headers) req.set(h.first, h.second);
            http::write(stream, req);
        }
        else {
            http::request<http::string_body> req{
                method, target, 11, std::string(body)
            };
            req.set(http::field::host, host);
            req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
            for (auto const& h : headers) req.set(h.first, h.second);
            req.set(http::field::content_length,
                    std::to_string(body.size()));
            http::write(stream, req);
        }

        return read_response(stream);
    }

} // namespace

std::string get(std::string_view host,
                std::string_view port,
                std::string_view target,
                std::map<std::string, std::string> const& headers,
                asio::io_context& ioc,
                asio::ssl::context& ssl_ctx)
{
    return sync_request(http::verb::get,
                        host, port, target, /*body=*/"",
                        headers, ioc, ssl_ctx);
}

std::string post(std::string_view host,
                 std::string_view port,
                 std::string_view target,
                 std::string_view body,
                 std::map<std::string, std::string> const& headers,
                 asio::io_context& ioc,
                 asio::ssl::context& ssl_ctx)
{
    return sync_request(http::verb::post,
                        host, port, target, body,
                        headers, ioc, ssl_ctx);
}

} // namespace http_client
