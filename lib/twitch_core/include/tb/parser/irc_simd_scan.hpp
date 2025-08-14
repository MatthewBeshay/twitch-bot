#pragma once
// Minimal SIMD helpers for IRC parsing.
// AVX2 and SSE2 on x86, optional NEON assist on AArch64, otherwise scalar.
// The goal is to scan 64 bytes at a time and build bitmasks for key chars.
//
// Notes
// - We never read past the available bytes. Near the end we copy to a
//   small scratch and load from there only when needed.
// - All comments use Australian English.
//
// API surface
//   struct CharMasks { uint64_t spaces, semicolons, equals, colons, letters_m, letters_b,
//   letters_u; } CharMasks scan64(const uint8_t* ptr, size_t n); uint32_t  pop_lowest(uint64_t&
//   bits); const char* find_space_in_tags_and_flags(const char* ptr, const char* endp,
//                                            uint8_t& is_mod, uint8_t& is_bc);

#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(__AVX2__) && (defined(__x86_64__) || defined(_M_X64))
#include <immintrin.h>
#define IRC_SIMD_AVX2 1
#elif (defined(__SSE2__) || defined(_M_X64) || defined(_M_IX86_FP))
#include <emmintrin.h>
#define IRC_SIMD_SSE2 1
#endif

#if defined(__aarch64__)
#include <arm_neon.h>
#define IRC_SIMD_NEON 1
#endif

namespace irc_simd {

struct CharMasks {
    uint64_t spaces;
    uint64_t semicolons;
    uint64_t equals;
    uint64_t colons;
    uint64_t letters_m; // for "mod=1"
    uint64_t letters_b; // for "badges="
    uint64_t letters_u; // for "user-type=mod"
};

#if defined(_MSC_VER)
static inline uint32_t ctz64(uint64_t x)
{
    unsigned long idx;
    _BitScanForward64(&idx, x);
    return static_cast<uint32_t>(idx);
}
#else
static inline uint32_t ctz64(uint64_t x)
{
    return static_cast<uint32_t>(__builtin_ctzll(x));
}
#endif

// Iterate set bits from low to high and clear the bit you returned.
static inline uint32_t pop_lowest(uint64_t& bits)
{
    uint32_t idx = ctz64(bits);
    bits &= bits - 1;
    return idx;
}

// Build masks for up to 64 bytes at ptr.
// n must be in [0, 64].
static inline CharMasks scan64(const uint8_t* ptr, size_t n)
{
    CharMasks out{0, 0, 0, 0, 0, 0, 0};

#if IRC_SIMD_AVX2
    // AVX2 path - direct loads for full lanes, copy only at the tail.
    auto movemask32 = [](__m256i v) -> uint32_t {
        __m128i lo = _mm256_castsi256_si128(v);
        __m128i hi = _mm256_extracti128_si256(v, 1);
        uint32_t mlo = static_cast<uint32_t>(_mm_movemask_epi8(lo));
        uint32_t mhi = static_cast<uint32_t>(_mm_movemask_epi8(hi));
        return mlo | (mhi << 16);
    };

    __m256i a, b;
    if (n >= 64) {
        a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr));
        b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr + 32));
    } else if (n >= 32) {
        a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(ptr));
        alignas(32) uint8_t tmp[32]{};
        std::memcpy(tmp, ptr + 32, n - 32);
        b = _mm256_load_si256(reinterpret_cast<const __m256i*>(tmp));
    } else {
        alignas(32) uint8_t tmpa[32]{};
        std::memcpy(tmpa, ptr, n);
        a = _mm256_load_si256(reinterpret_cast<const __m256i*>(tmpa));
        b = _mm256_setzero_si256();
    }

    auto build_mask = [&](unsigned char c) -> uint64_t {
        __m256i vv = _mm256_set1_epi8(static_cast<char>(c));
        uint32_t lo = movemask32(_mm256_cmpeq_epi8(a, vv));
        uint32_t hi = movemask32(_mm256_cmpeq_epi8(b, vv));
        return (static_cast<uint64_t>(lo)) | (static_cast<uint64_t>(hi) << 32);
    };

    out.spaces = build_mask(' ');
    out.semicolons = build_mask(';');
    out.equals = build_mask('=');
    out.colons = build_mask(':');
    out.letters_m = build_mask('m');
    out.letters_b = build_mask('b');
    out.letters_u = build_mask('u');

#elif IRC_SIMD_SSE2
    // SSE2 path: four 16 byte loads build a 64 bit mask.
    alignas(16) uint8_t buf[64]{};
    if (n)
        std::memcpy(buf, ptr, n);

    auto mask16 = [](const __m128i v, unsigned char c) -> uint16_t {
        __m128i m = _mm_cmpeq_epi8(v, _mm_set1_epi8(static_cast<char>(c)));
        return static_cast<uint16_t>(_mm_movemask_epi8(m));
    };

    uint64_t m_space = 0, m_semi = 0, m_eq = 0, m_col = 0, m_m = 0, m_b = 0, m_u = 0;
    for (int i = 0; i < 4; ++i) {
        const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(buf + i * 16));
        const uint64_t sh = static_cast<uint64_t>(i * 16);
        m_space |= static_cast<uint64_t>(mask16(v, ' ')) << sh;
        m_semi |= static_cast<uint64_t>(mask16(v, ';')) << sh;
        m_eq |= static_cast<uint64_t>(mask16(v, '=')) << sh;
        m_col |= static_cast<uint64_t>(mask16(v, ':')) << sh;
        m_m |= static_cast<uint64_t>(mask16(v, 'm')) << sh;
        m_b |= static_cast<uint64_t>(mask16(v, 'b')) << sh;
        m_u |= static_cast<uint64_t>(mask16(v, 'u')) << sh;
    }
    out.spaces = m_space;
    out.semicolons = m_semi;
    out.equals = m_eq;
    out.colons = m_col;
    out.letters_m = m_m;
    out.letters_b = m_b;
    out.letters_u = m_u;

#elif IRC_SIMD_NEON
    // NEON assist: vector compare then compress to a bitmask in scalar.
    uint8_t buf[64]{};
    if (n)
        std::memcpy(buf, ptr, n);
    auto pack16 = [](const uint8_t* p, unsigned char c) -> uint16_t {
        uint8x16_t v = vld1q_u8(p);
        uint8x16_t cmp = vceqq_u8(v, vdupq_n_u8(c));
        uint8_t tmp[16];
        vst1q_u8(tmp, cmp);
        uint16_t mask = 0;
        for (int i = 0; i < 16; ++i) {
            mask |= static_cast<uint16_t>((tmp[i] >> 7) & 1u) << i;
        }
        return mask;
    };
    uint64_t m_space = 0, m_semi = 0, m_eq = 0, m_col = 0, m_m = 0, m_b = 0, m_u = 0;
    for (int i = 0; i < 4; ++i) {
        const uint8_t* p = buf + i * 16;
        const uint64_t sh = static_cast<uint64_t>(i * 16);
        m_space |= static_cast<uint64_t>(pack16(p, ' ')) << sh;
        m_semi |= static_cast<uint64_t>(pack16(p, ';')) << sh;
        m_eq |= static_cast<uint64_t>(pack16(p, '=')) << sh;
        m_col |= static_cast<uint64_t>(pack16(p, ':')) << sh;
        m_m |= static_cast<uint64_t>(pack16(p, 'm')) << sh;
        m_b |= static_cast<uint64_t>(pack16(p, 'b')) << sh;
        m_u |= static_cast<uint64_t>(pack16(p, 'u')) << sh;
    }
    out.spaces = m_space;
    out.semicolons = m_semi;
    out.equals = m_eq;
    out.colons = m_col;
    out.letters_m = m_m;
    out.letters_b = m_b;
    out.letters_u = m_u;

#else
    // Scalar fallback - branch light and predictable.
    for (size_t i = 0; i < n; ++i) {
        const unsigned char ch = ptr[i];
        out.spaces |= uint64_t(ch == ' ') << i;
        out.semicolons |= uint64_t(ch == ';') << i;
        out.equals |= uint64_t(ch == '=') << i;
        out.colons |= uint64_t(ch == ':') << i;
        out.letters_m |= uint64_t(ch == 'm') << i;
        out.letters_b |= uint64_t(ch == 'b') << i;
        out.letters_u |= uint64_t(ch == 'u') << i;
    }
#endif

    // Mask out any bits beyond n.
    if (n < 64) {
        const uint64_t live = (n == 0) ? 0ull : ((1ull << n) - 1ull);
        out.spaces &= live;
        out.semicolons &= live;
        out.equals &= live;
        out.colons &= live;
        out.letters_m &= live;
        out.letters_b &= live;
        out.letters_u &= live;
    }
    return out;
}

// Simple substring search for a small needle, no locale, no UB.
static inline bool contains_broadcaster_1(const char* hay, const char* hay_end)
{
    static constexpr char needle[] = "broadcaster/1";
    static constexpr size_t L = sizeof(needle) - 1;
    const size_t n = static_cast<size_t>(hay_end - hay);
    if (n < L)
        return false;
    for (size_t i = 0; i + L <= n; ++i) {
        if (std::memcmp(hay + i, needle, L) == 0)
            return true;
    }
    return false;
}

// Find the first space ending the tag block while updating moderator and broadcaster flags.
static inline const char*
find_space_in_tags_and_flags(const char* ptr, const char* endp, uint8_t& is_mod, uint8_t& is_bc)
{
    is_mod = 0;
    is_bc = 0;

    const char* scan = ptr;
    while (scan < endp) {
        const size_t remain = static_cast<size_t>(endp - scan);
        const size_t chunk = remain < 64 ? remain : size_t(64);
        const CharMasks m = scan64(reinterpret_cast<const uint8_t*>(scan), chunk);

        // First space ends the tag block.
        if (m.spaces) {
            const uint32_t off = ctz64(m.spaces);
            // Harvest signals in this chunk before exiting.

            // "mod=1"
            uint64_t mm = m.letters_m;
            while (mm && !is_mod) {
                const uint32_t i = pop_lowest(mm);
                const char* s = scan + i;
                if (endp - s >= 5 && std::memcmp(s, "mod=1", 5) == 0) {
                    is_mod = 1;
                }
            }
            // "user-type=mod"
            uint64_t uu = m.letters_u;
            while (uu && !is_mod) {
                const uint32_t i = pop_lowest(uu);
                const char* s = scan + i;
                if (endp - s >= 13 && std::memcmp(s, "user-type=mod", 13) == 0) {
                    is_mod = 1;
                }
            }
            // badges value contains "broadcaster/1"
            uint64_t bb = m.letters_b;
            while (bb && !is_bc) {
                const uint32_t i = pop_lowest(bb);
                const char* s = scan + i;
                if (endp - s >= 7 && std::memcmp(s, "badges=", 7) == 0) {
                    const char* val = s + 7;
                    // Value ends at next semicolon or at the space.
                    const char* stop = val;
                    for (;;) {
                        const size_t r = static_cast<size_t>(endp - stop);
                        if (r == 0)
                            break;
                        const size_t c = r < 64 ? r : size_t(64);
                        const CharMasks mv = scan64(reinterpret_cast<const uint8_t*>(stop), c);
                        const uint64_t enders = mv.semicolons | mv.spaces;
                        const size_t slice_len = enders ? static_cast<size_t>(ctz64(enders)) : c;
                        const char* slice_end = stop + slice_len;
                        if (contains_broadcaster_1(stop, slice_end)) {
                            is_bc = 1;
                            break;
                        }
                        if (enders)
                            break;
                        stop += c;
                    }
                }
            }
            return scan + off;
        }

        // No space yet - continue scanning this chunk.

        // "mod=1"
        uint64_t mm = m.letters_m;
        while (mm && !is_mod) {
            const uint32_t i = pop_lowest(mm);
            const char* s = scan + i;
            if (endp - s >= 5 && std::memcmp(s, "mod=1", 5) == 0) {
                is_mod = 1;
            }
        }
        // "user-type=mod"
        uint64_t uu = m.letters_u;
        while (uu && !is_mod) {
            const uint32_t i = pop_lowest(uu);
            const char* s = scan + i;
            if (endp - s >= 13 && std::memcmp(s, "user-type=mod", 13) == 0) {
                is_mod = 1;
            }
        }
        // badges value contains "broadcaster/1"
        uint64_t bb = m.letters_b;
        while (bb && !is_bc) {
            const uint32_t i = pop_lowest(bb);
            const char* s = scan + i;
            if (endp - s >= 7 && std::memcmp(s, "badges=", 7) == 0) {
                const char* val = s + 7;
                const char* stop = val;
                for (;;) {
                    const size_t r = static_cast<size_t>(endp - stop);
                    if (r == 0)
                        break;
                    const size_t c = r < 64 ? r : size_t(64);
                    const CharMasks mv = scan64(reinterpret_cast<const uint8_t*>(stop), c);
                    const uint64_t enders = mv.semicolons | mv.spaces;
                    const size_t slice_len = enders ? static_cast<size_t>(ctz64(enders)) : c;
                    const char* slice_end = stop + slice_len;
                    if (contains_broadcaster_1(stop, slice_end)) {
                        is_bc = 1;
                        break;
                    }
                    if (enders)
                        break;
                    stop += c;
                }
            }
        }

        // Advance to next 64 byte window.
        scan += chunk;
    }
    // No space - the whole rest is raw tags.
    return endp;
}

} // namespace irc_simd
