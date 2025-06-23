#pragma once

#include <algorithm>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

// State bits for chunked decoder
constexpr uint64_t STATE_HAS_SIZE = 1ull << (sizeof(uint64_t) * 8 - 1); // 0x80000000;
constexpr uint64_t STATE_IS_CHUNKED = 1ull << (sizeof(uint64_t) * 8 - 2); // 0x40000000;
constexpr uint64_t STATE_SIZE_MASK = ~(3ull << (sizeof(uint64_t) * 8 - 2)); // 0x3FFFFFFF;
constexpr uint64_t STATE_IS_ERROR = ~0ull; // 0xFFFFFFFF;
constexpr uint64_t STATE_SIZE_OVERFLOW = 0x0Full << (sizeof(uint64_t) * 8 - 8); // 0x0F000000;

inline uint64_t chunk_size(uint64_t state)
{
    return state & STATE_SIZE_MASK;
}

// Parse a hex chunk-size header and update state when a CRLF is found.
inline void consume_hex_number(std::string_view &data, uint64_t &state)
{
    uint64_t preserved_flags = state & STATE_IS_CHUNKED;
    uint64_t size = 0;

    // Accumulate hex digits until a non-hex char
    while (!data.empty()) {
        unsigned char c = static_cast<unsigned char>(data.front());
        unsigned int digit;
        if (c >= '0' && c <= '9')
            digit = c - '0';
        else if (c >= 'A' && c <= 'F')
            digit = c - 'A' + 10;
        else if (c >= 'a' && c <= 'f')
            digit = c - 'a' + 10;
        else
            break;

        // Overflow guard
        if (size > (STATE_SIZE_MASK >> 4) || ((size << 4) | digit) > STATE_SIZE_MASK) {
            state = STATE_IS_ERROR;
            return;
        }

        size = (size << 4) | digit;
        data.remove_prefix(1);
    }

    // Require CRLF to complete the header
    if (data.size() < 2 || data[0] != '\r' || data[1] != '\n') {
        return;
    }
    data.remove_prefix(2);

    // Store size + CRLF length, preserve chunked bit
    state = (size + 2) | STATE_HAS_SIZE | preserved_flags;
}

inline void dec_chunk_size(uint64_t &state, unsigned int by)
{
    state = (state & ~STATE_SIZE_MASK) | (chunk_size(state) - by);
}

inline bool has_chunk_size(uint64_t state)
{
    return (state & STATE_HAS_SIZE) != 0;
}

// True while in the middle of chunked-body or trailer parsing.
inline bool is_parsing_chunked_encoding(uint64_t state)
{
    return (state & ~STATE_SIZE_MASK) != 0;
}

inline bool is_parsing_invalid_chunked_encoding(uint64_t state)
{
    return state == STATE_IS_ERROR;
}

// Extract the next data payload from `data`.
// Returns nullopt when no more chunks remain.
static std::optional<std::string_view>
get_next_chunk(std::string_view &data, uint64_t &state, bool trailer = false)
{
    while (!data.empty()) {

        // Skip trailer bytes when in trailer mode
        if (!(state & STATE_IS_CHUNKED) && has_chunk_size(state) && chunk_size(state) > 0) {
            while (!data.empty() && chunk_size(state) > 0) {
                data.remove_prefix(1);
                dec_chunk_size(state, 1);
                if (chunk_size(state) == 0) {
                    state = 0;
                    return std::nullopt;
                }
            }
            continue;
        }

        // Read a new chunk-size header if needed
        if (!has_chunk_size(state)) {
            consume_hex_number(data, state);
            if (is_parsing_invalid_chunked_encoding(state)) {
                return std::nullopt;
            }
            // Empty chunk header (CRLF only) signals an empty chunk
            if (has_chunk_size(state) && chunk_size(state) == 2) {
                state = (trailer ? 4 : 2) | STATE_HAS_SIZE;
                return std::string_view(nullptr, 0);
            }
            continue;
        }

        // Emit a full chunk if all bytes are available
        uint64_t size = chunk_size(state);
        if (data.size() >= size) {
            std::string_view chunk;
            if (size > 2) {
                chunk = std::string_view(data.data(), size - 2);
            }
            data.remove_prefix(static_cast<size_t>(size));
            state = STATE_IS_CHUNKED;
            if (!chunk.empty()) {
                return chunk;
            }
            continue;
        }

        // Emit a partial chunk if only part of the payload is available
        size_t payload = (size > 2 ? static_cast<size_t>(size - 2) : 0);
        size_t consume = std::min(data.size(), payload);
        if (consume > 0) {
            std::string_view chunk = data.substr(0, consume);
            dec_chunk_size(state, static_cast<unsigned>(consume));
            state |= STATE_IS_CHUNKED;
            data.remove_prefix(consume);
            return chunk;
        }

        // No payload bytes left (just CRLF): reset to read next header
        state = 0;
        return std::nullopt;
    }

    return std::nullopt;
}

// Facilitate range-based for over decoded chunks.
struct ChunkIterator {
    std::string_view *data;
    std::optional<std::string_view> chunk;
    uint64_t *state;
    bool trailer;

    ChunkIterator(std::string_view *data, uint64_t *state, bool trailer = false)
        : data(data), state(state), trailer(trailer)
    {
        chunk = get_next_chunk(*data, *state, trailer);
    }

    ChunkIterator() = default;

    ChunkIterator begin()
    {
        return *this;
    }
    ChunkIterator end()
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
        chunk = get_next_chunk(*data, *state, trailer);
        return *this;
    }
};
