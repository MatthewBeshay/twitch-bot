#pragma once

// Cross-platform attributes:
//   TB_FORCE_INLINE - request aggressive inlining
//   TB_RESTRICT     - indicate a pointer does not alias others
//
// MSVC:        __forceinline / __restrict
// GCC/Clang:   inline __attribute__((always_inline)) / __restrict or __restrict__
// C99:         inline / restrict
// Fallback:    inline / (nothing)

// Allow projects to override via -DTB_NO_FORCE_INLINE
#if !defined(TB_NO_FORCE_INLINE)

// Clang and newer GCC expose __has_attribute; provide a safe default.
#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

#if defined(_MSC_VER)
#define TB_FORCE_INLINE __forceinline
#elif defined(__clang__) || defined(__GNUC__)
// GCC/Clang require 'inline' plus the attribute for best effect.
#if __has_attribute(always_inline) || defined(__GNUC__)
#define TB_FORCE_INLINE inline __attribute__((always_inline))
#else
#define TB_FORCE_INLINE inline
#endif
#else
#define TB_FORCE_INLINE inline
#endif

#else
#define TB_FORCE_INLINE inline
#endif // !TB_NO_FORCE_INLINE

// --- TB_RESTRICT -------------------------------------------------------------

// C has standard 'restrict' (C99). In C++, use vendor extensions.
// Prefer plain 'restrict' in C mode where available; in C++ use __restrict/__restrict__.
//
// Note: Do not "lie" with restrict; violating no-alias promises is UB.

#if defined(__cplusplus)
#if defined(_MSC_VER)
#define TB_RESTRICT __restrict
#elif defined(__clang__) || defined(__GNUC__)
#define TB_RESTRICT __restrict__
#else
#define TB_RESTRICT
#endif
#else // C
#if defined(_MSC_VER)
#define TB_RESTRICT __restrict
#else
#define TB_RESTRICT restrict
#endif
#endif
