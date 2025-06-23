#pragma once

// C++ Standard Library
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>

// Project
#include "utils/attributes.hpp"

// State bits for chunked decoder
constexpr uint64_t STATE_HAS_SIZE = 1ull << (sizeof(uint64_t) * 8 - 1); // 0x80000000
constexpr uint64_t STATE_IS_CHUNKED = 1ull << (sizeof(uint64_t) * 8 - 2); // 0x40000000
constexpr uint64_t STATE_SIZE_MASK = ~(3ull << (sizeof(uint64_t) * 8 - 2)); // 0x3FFFFFFF
constexpr uint64_t STATE_IS_ERROR = ~0ull; // 0xFFFFFFFF
constexpr uint64_t STATE_SIZE_OVERFLOW = 0x0Full << (sizeof(uint64_t) * 8 - 8); // 0x0F000000

// Build hex-lookup table at compile time
static constexpr auto make_hex_val()
{
    std::array<unsigned char, 256> tbl{};
    tbl.fill(0xFF);

    // '0'–'9'
    for (unsigned char c = '0'; c <= '9'; ++c) {
        tbl[c] = c - '0';
    }
    // 'A'–'F'
    for (unsigned char c = 'A'; c <= 'F'; ++c) {
        tbl[c] = 10 + (c - 'A');
    }
    // 'a'–'f'
    for (unsigned char c = 'a'; c <= 'f'; ++c) {
        tbl[c] = 10 + (c - 'a');
    }
    return tbl;
}

static constexpr std::array<unsigned char, 256> HEX_VAL = make_hex_val();

// Extract the size bits
TB_FORCE_INLINE uint64_t chunk_size(uint64_t state)
{
    return state & STATE_SIZE_MASK;
}

// Decrement the size counter
TB_FORCE_INLINE void dec_chunk_size(uint64_t &state, unsigned by)
{
    state = (state & ~STATE_SIZE_MASK) | (chunk_size(state) - by);
}

// Has the size header been parsed?
TB_FORCE_INLINE bool has_chunk_size(uint64_t state)
{
    return (state & STATE_HAS_SIZE) != 0;
}

// True while in the middle of chunked-body or trailer parsing.
TB_FORCE_INLINE bool is_parsing_chunked_encoding(uint64_t state)
{
    return (state & ~STATE_SIZE_MASK) != 0;
}

TB_FORCE_INLINE bool is_parsing_invalid_chunked_encoding(uint64_t state)
{
    return state == STATE_IS_ERROR;
}

// Parse a hex chunk-size header from ptr/len and update state when CRLF is found.
TB_FORCE_INLINE void consume_hex_number(const char *TB_RESTRICT &ptr, size_t &len, uint64_t &state)
{
    uint64_t preserved_flags = state & STATE_IS_CHUNKED;
    uint64_t size_accum = 0;

    // Accumulate hex digits
    while (len > 0) {
        unsigned char c = static_cast<unsigned char>(*ptr);
        unsigned char v = HEX_VAL[c];
        if (v == 0xFF) // not a hex digit
            break;

        // Overflow guard
        if (size_accum > (STATE_SIZE_MASK >> 4) || ((size_accum << 4) | v) > STATE_SIZE_MASK) {
            state = STATE_IS_ERROR;
            return;
        }

        size_accum = (size_accum << 4) | v;
        ptr++;
        len--;
    }

    // Need at least CRLF
    if (len < 2 || ptr[0] != '\r' || ptr[1] != '\n') {
        return;
    }
    ptr += 2;
    len -= 2;

    // Store size + CRLF length, preserve chunked flag
    state = (size_accum + 2) | STATE_HAS_SIZE | preserved_flags;
}

// Extract the next data payload from ptr/len.
// Returns nullopt when no more chunks remain.
TB_FORCE_INLINE std::optional<std::string_view>
get_next_chunk(const char *TB_RESTRICT &ptr, size_t &len, uint64_t &state, bool trailer = false)
{
    while (len > 0) {

        // Skip trailer bytes
        if (!(state & STATE_IS_CHUNKED) && has_chunk_size(state) && chunk_size(state) > 0) {
            while (len > 0 && chunk_size(state) > 0) {
                ptr++;
                len--;
                dec_chunk_size(state, 1);
                if (chunk_size(state) == 0) {
                    state = 0;
                    return std::nullopt;
                }
            }
            continue;
        }

        // Parse a new chunk-size header
        if (!has_chunk_size(state)) {
            consume_hex_number(ptr, len, state);
            if (is_parsing_invalid_chunked_encoding(state)) {
                return std::nullopt;
            }
            // Empty-chunk signal (CRLF only)
            if (has_chunk_size(state) && chunk_size(state) == 2) {
                state = (trailer ? 4 : 2) | STATE_HAS_SIZE;
                return std::string_view(nullptr, 0);
            }
            continue;
        }

        // Full chunk available?
        {
            uint64_t sz = chunk_size(state);
            if (len >= sz) {
                std::string_view chunk;
                if (sz > 2) {
                    chunk = std::string_view(ptr, sz - 2);
                }
                ptr += sz;
                len -= static_cast<size_t>(sz);
                state = STATE_IS_CHUNKED;
                if (!chunk.empty()) {
                    return chunk;
                }
                continue;
            }
        }

        // Partial chunk
        {
            uint64_t sz = chunk_size(state);
            size_t payload = (sz > 2 ? static_cast<size_t>(sz - 2) : 0);
            size_t nconsume = std::min(len, payload);
            if (nconsume > 0) {
                std::string_view chunk(ptr, nconsume);
                ptr += nconsume;
                len -= nconsume;
                dec_chunk_size(state, static_cast<unsigned>(nconsume));
                state |= STATE_IS_CHUNKED;
                return chunk;
            }
        }

        // Only CRLF left—reset
        state = 0;
        return std::nullopt;
    }

    return std::nullopt;
}

// Facilitate range-based for over decoded chunks.
struct ChunkIterator {
    const char *TB_RESTRICT ptr; // current read pointer
    size_t len; // remaining length
    uint64_t *state; // pointer into external parser state
    bool trailer; // trailer mode flag
    std::optional<std::string_view> chunk;

    // construct with initial buffer ptr/len and state
    ChunkIterator(const char *TB_RESTRICT input_ptr,
                  size_t input_len,
                  uint64_t &input_state,
                  bool trailer_mode = false)
        : ptr(input_ptr), len(input_len), state(&input_state), trailer(trailer_mode)
    {
        chunk = get_next_chunk(ptr, len, *state, trailer);
    }

    // end-iterator sentinel
    ChunkIterator() : ptr(nullptr), len(0), state(nullptr), trailer(false), chunk(std::nullopt)
    {
    }

    // for range-based for
    ChunkIterator begin() const
    {
        return *this;
    }
    ChunkIterator end() const
    {
        return {};
    }

    std::string_view operator*() const
    {
        if (!chunk)
            std::abort();
        return *chunk;
    }

    bool operator!=(const ChunkIterator &other) const
    {
        return static_cast<bool>(chunk) != static_cast<bool>(other.chunk);
    }

    ChunkIterator &operator++()
    {
        // Only called when chunk has value and state!=nullptr
        chunk = get_next_chunk(ptr, len, *state, trailer);
        return *this;
    }
};
