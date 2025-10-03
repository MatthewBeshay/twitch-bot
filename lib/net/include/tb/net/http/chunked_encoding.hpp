/*
Module Name:
- chunked_encoding.hpp

Abstract:
- Minimal, resumable HTTP chunked transfer decoder.
- Packs flags and remaining byte count into one 64-bit state word to avoid extra fields.
- Accepts input in arbitrary splits and yields payload slices without copying.
- Supports chunk extensions and optional trailer consumption.
- Uses TB_* hints for predictable codegen and zero allocations.
*/
#pragma once

// C++ Standard Library
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <string_view>

// Core
#include <tb/utils/attributes.hpp>

// State layout (high to low bits):
// - bit 63: STATE_HAS_SIZE - size header has been parsed for the current chunk
// - bit 62: STATE_IS_CHUNKED - we are inside a chunk payload
// - bits 0..61: remaining byte count for current chunk including its trailing CRLF
// The all-ones pattern is reserved as an error sentinel.
static_assert(std::numeric_limits<std::uint64_t>::digits == 64);

constexpr std::uint64_t STATE_HAS_SIZE = (std::uint64_t{ 1 } << 63);
constexpr std::uint64_t STATE_IS_CHUNKED = (std::uint64_t{ 1 } << 62);
constexpr std::uint64_t STATE_SIZE_MASK = ~(std::uint64_t{ 3 } << 62);
constexpr std::uint64_t STATE_IS_ERROR = ~std::uint64_t{ 0 };

constexpr std::uint64_t CRLF_LEN = 2;

// Build hex-lookup table at compile time to avoid branching per digit.
static constexpr auto make_hex_val()
{
    std::array<unsigned char, 256> tbl{};
    tbl.fill(0xFF);

    for (unsigned char c = '0'; c <= '9'; ++c)
    {
        tbl[c] = c - '0';
    }
    for (unsigned char c = 'A'; c <= 'F'; ++c)
    {
        tbl[c] = 10 + (c - 'A');
    }
    for (unsigned char c = 'a'; c <= 'f'; ++c)
    {
        tbl[c] = 10 + (c - 'a');
    }
    return tbl;
}

static constexpr std::array<unsigned char, 256> HEX_VAL = make_hex_val();

TB_FORCE_INLINE uint64_t chunk_size(uint64_t state) noexcept
{
    return state & STATE_SIZE_MASK;
}

TB_FORCE_INLINE void dec_chunk_size(uint64_t& state, uint64_t by) noexcept
{
    state = (state & ~STATE_SIZE_MASK) | (chunk_size(state) - by);
}

TB_FORCE_INLINE bool has_chunk_size(uint64_t state) noexcept
{
    return (state & STATE_HAS_SIZE) != 0;
}

// True while still parsing a chunk or trailers.
TB_FORCE_INLINE bool is_parsing_chunked_encoding(uint64_t state) noexcept
{
    return (state & ~STATE_SIZE_MASK) != 0;
}

TB_FORCE_INLINE bool is_parsing_invalid_chunked_encoding(uint64_t state) noexcept
{
    return state == STATE_IS_ERROR;
}

// Parse a hex chunk-size header from ptr/len and update state when CRLF is found.
// Supports optional chunk extensions: "<hex>[;ext...]\r\n"
TB_FORCE_INLINE void
consume_hex_number(const char* TB_RESTRICT& ptr, size_t& len, uint64_t& state) noexcept
{
    const uint64_t preserved_flags = state & STATE_IS_CHUNKED;
    uint64_t size_accum = 0;
    bool saw_digit = false;

    // Accumulate hex digits with overflow guard against the size field.
    while (len > 0)
    {
        const unsigned char c = static_cast<unsigned char>(*ptr);
        const unsigned char v = HEX_VAL[c];
        if (v == 0xFF)
        {
            break;
        }

        if (size_accum > (STATE_SIZE_MASK >> 4) || ((size_accum << 4) | v) > STATE_SIZE_MASK)
        {
            state = STATE_IS_ERROR; // size would not fit the state word
            return;
        }
        size_accum = (size_accum << 4) | v;
        ++ptr;
        --len;
        saw_digit = true;
    }

    if (!saw_digit)
    {
        state = STATE_IS_ERROR; // header had no hex digits
        return;
    }

    // Skip any extensions until CR.
    while (len > 0 && *ptr != '\r')
    {
        ++ptr;
        --len;
    }

    // Require CRLF to finish the header.
    if (len < 2 || ptr[0] != '\r' || ptr[1] != '\n')
    {
        return; // need more bytes
    }
    ptr += 2;
    len -= 2;

    // Store remaining = payload + CRLF, keep the mode flag for partial payloads.
    state = (size_accum + 2) | STATE_HAS_SIZE | preserved_flags;
}

// Extract the next data payload from ptr/len.
// Returns nullopt when waiting for more data or once trailers are consumed.
// Returns an empty view exactly at the zero-size chunk marker.
TB_FORCE_INLINE std::optional<std::string_view> get_next_chunk(const char* TB_RESTRICT& ptr,
                                                               size_t& len,
                                                               uint64_t& state,
                                                               bool trailer = false) noexcept
{
    while (len > 0)
    {
        // After zero-size chunk, skip trailer bytes if requested.
        if (!(state & STATE_IS_CHUNKED) && has_chunk_size(state) && chunk_size(state) > 0)
        {
            while (len > 0 && chunk_size(state) > 0)
            {
                ++ptr;
                --len;
                dec_chunk_size(state, 1);
                if (chunk_size(state) == 0)
                {
                    state = 0;
                    return std::nullopt;
                }
            }
            return std::nullopt; // need more data to finish skipping
        }

        // Parse a new chunk-size header when needed.
        if (!has_chunk_size(state))
        {
            consume_hex_number(ptr, len, state);
            if (is_parsing_invalid_chunked_encoding(state))
            {
                return std::nullopt;
            }
            // Empty chunk: only terminators and optional trailers remain.
            if (has_chunk_size(state) && chunk_size(state) == CRLF_LEN)
            {
                state = ((trailer ? 4ull : 2ull) | STATE_HAS_SIZE);
                return {}; // marks end-of-chunks
            }
            continue;
        }

        // Full chunk available.
        {
            const uint64_t sz = chunk_size(state);
            if (len >= sz)
            {
                std::string_view chunk;
                if (sz > CRLF_LEN)
                {
                    chunk = std::string_view(ptr, static_cast<size_t>(sz - CRLF_LEN));
                }
                ptr += sz;
                len -= static_cast<size_t>(sz);
                state = STATE_IS_CHUNKED;
                if (!chunk.empty())
                {
                    return chunk;
                }
                continue; // no payload, move to next header
            }
        }

        // Partial payload available.
        {
            const uint64_t sz = chunk_size(state);
            const size_t payload = (sz > CRLF_LEN ? static_cast<size_t>(sz - CRLF_LEN) : 0u);
            const size_t nconsume = std::min(len, payload);
            if (nconsume > 0)
            {
                std::string_view chunk(ptr, nconsume);
                ptr += nconsume;
                len -= nconsume;
                dec_chunk_size(state, static_cast<uint64_t>(nconsume));
                state |= STATE_IS_CHUNKED;
                return chunk;
            }
        }

        // Only partial or full CRLF remains for this chunk.
        return std::nullopt;
    }

    return std::nullopt;
}

// Iterator to facilitate range-based for over decoded chunks.
// Empty view indicates the terminal zero-size chunk.
struct ChunkIterator
{
    const char* TB_RESTRICT ptr;
    size_t len;
    uint64_t* state;
    bool trailer;
    std::optional<std::string_view> chunk;

    ChunkIterator(const char* TB_RESTRICT input_ptr,
                  size_t input_len,
                  uint64_t& input_state,
                  bool trailer_mode = false) :
        ptr(input_ptr), len(input_len), state(&input_state), trailer(trailer_mode)
    {
        chunk = get_next_chunk(ptr, len, *state, trailer);
    }

    // Sentinel for end.
    ChunkIterator() :
        ptr(nullptr), len(0), state(nullptr), trailer(false), chunk(std::nullopt)
    {
    }

    ChunkIterator begin() const { return *this; }
    ChunkIterator end() const { return {}; }

    std::string_view operator*() const
    {
        if (!chunk)
        {
            std::abort(); // misuse: deref after end
        }
        return *chunk;
    }

    bool operator!=(const ChunkIterator& other) const
    {
        return static_cast<bool>(chunk) != static_cast<bool>(other.chunk);
    }

    ChunkIterator& operator++()
    {
        chunk = get_next_chunk(ptr, len, *state, trailer);
        return *this;
    }
};
