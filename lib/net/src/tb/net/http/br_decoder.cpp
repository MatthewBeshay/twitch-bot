// C++ Standard Library
#include <string>
#include <string_view>
#include <system_error>

// Brotli
#include <brotli/decode.h>

// Project
#include <tb/net/http/encoding.hpp>
#include <tb/net/http/error.hpp>

namespace tb::net::encoding
{

    bool br_decode(std::string_view in, std::string& out, std::error_code& ec)
    {
        BrotliDecoderState* st = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);
        if (!st)
        {
            ec = errc::decompression_failure;
            return false;
        }

        const uint8_t* next_in = reinterpret_cast<const uint8_t*>(in.data());
        size_t avail_in = in.size();

        out.clear();
        uint8_t buf[1 << 14]; // 16 KiB scratch

        for (;;)
        {
            uint8_t* next_out = buf;
            size_t avail_out = sizeof(buf);

            const BrotliDecoderResult r = BrotliDecoderDecompressStream(st, &avail_in, &next_in, &avail_out, &next_out, nullptr);

            const size_t produced = static_cast<size_t>(next_out - buf);
            if (produced)
                out.append(reinterpret_cast<const char*>(buf), produced);

            if (r == BROTLI_DECODER_RESULT_SUCCESS)
            {
                BrotliDecoderDestroyInstance(st);
                ec.clear();
                return true;
            }
            if (r == BROTLI_DECODER_RESULT_ERROR)
            {
                BrotliDecoderDestroyInstance(st);
                ec = errc::decompression_failure;
                return false;
            }
            // NEEDS_MORE_INPUT / NEEDS_MORE_OUTPUT → loop again until success/error
        }
    }

} // namespace tb::net::encoding
