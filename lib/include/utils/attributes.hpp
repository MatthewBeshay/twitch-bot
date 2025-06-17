#pragma once

// Cross-platform attributes:
//   TB_FORCE_INLINE – request aggressive inlining
//   TB_RESTRICT     – indicate a pointer does not alias others
//
// MSVC:   __forceinline / __restrict
// GCC/Clang: inline __attribute__((always_inline)) / __restrict__
// Fallback: inline / (nothing)
#if defined(_MSC_VER)
#define TB_FORCE_INLINE __forceinline
#define TB_RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__)
#define TB_FORCE_INLINE inline __attribute__((always_inline))
#define TB_RESTRICT __restrict__
#else
#define TB_FORCE_INLINE inline
#define TB_RESTRICT
#endif
