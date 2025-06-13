#pragma once

// Attribute macros for cross-platform “always inline” and aliasing hints.
// Uses compiler-specific keywords to help the optimizer.

// TB_FORCE_INLINE:
//   Requests aggressive inlining.
//   - MSVC:   __forceinline
//   - GCC/Clang: inline __attribute__((always_inline))
//   - Others: inline
//
// TB_RESTRICT:
//   Declares that a pointer does not alias other pointers.
//   - MSVC:   __restrict
//   - GCC/Clang: __restrict__
//   - Others: (empty)
#if defined(_MSC_VER)
#  define TB_FORCE_INLINE __forceinline
#  define TB_RESTRICT     __restrict
#elif defined(__GNUC__) || defined(__clang__)
#  define TB_FORCE_INLINE inline __attribute__((always_inline))
#  define TB_RESTRICT     __restrict__
#else
#  define TB_FORCE_INLINE inline
#  define TB_RESTRICT
#endif
