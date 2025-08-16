// C++ Standard Library
#include <limits>
#include <string>
#include <string_view>
#include <system_error>

// Zlib
#include <zlib.h>

// Project
#include <tb/net/http/encoding.hpp>
#include <tb/net/http/error.hpp>

namespace tb::net::encoding {

bool gzip_decode(std::string_view in, std::string& out, std::error_code& ec)
{
    z_stream zs{};
    zs.zalloc = nullptr;
    zs.zfree = nullptr;
    zs.opaque = nullptr;

    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
    zs.avail_in = static_cast<uInt>(in.size());

    const int init = inflateInit2(&zs, 16 + MAX_WBITS);
    if (init != Z_OK) {
        ec = errc::decompression_failure;
        return false;
    }

    out.clear();
    unsigned char buf[1 << 14]; // 16 KiB scratch
    int ret = Z_OK;

    do {
        zs.next_out = buf;
        zs.avail_out = static_cast<uInt>(sizeof(buf));

        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&zs);
            ec = errc::decompression_failure;
            return false;
        }

        const std::size_t produced = sizeof(buf) - zs.avail_out;
        if (produced)
            out.append(reinterpret_cast<const char*>(buf), produced);
    } while (ret != Z_STREAM_END);

    inflateEnd(&zs);
    ec.clear();
    return true;
}

} // namespace tb::net::encoding
