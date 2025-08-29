#pragma once

// Cross-platform attributes and branch hints.
// All comments use Australian English and ASCII only.
//
// Provided macros:
//   TB_FORCE_INLINE  - request aggressive inlining
//   TB_NOINLINE      - prevent inlining
//   TB_RESTRICT      - indicate a pointer does not alias others
//   TB_LIKELY(x)     - branch prediction hint for likely condition
//   TB_UNLIKELY(x)   - branch prediction hint for unlikely condition
//   TB_ASSUME(x)     - tell the compiler the condition is always true
//   TB_UNREACHABLE() - mark code as unreachable
//
// Notes:
// - TB_LIKELY and TB_UNLIKELY are expression context hints.
// - TB_FALLTHROUGH must be used as a statement in a switch case.
// - Do not lie with TB_RESTRICT or TB_ASSUME. Violations are UB.
//
// Compilers:
//   MSVC:        __forceinline / __declspec(noinline) / __assume / __restrict
//   GCC/Clang:   inline __attribute__((always_inline)) / __attribute__((noinline))
//                __builtin_expect / __builtin_assume or fallback / __restrict__
//   C mode:      'restrict' where available

// -----------------------------------------------------------------------------
// feature-test helpers

#ifndef __has_attribute
#define __has_attribute(x) 0
#endif

#ifndef __has_cpp_attribute
#define __has_cpp_attribute(x) 0
#endif

// -----------------------------------------------------------------------------
// TB_FORCE_INLINE

#if !defined(TB_NO_FORCE_INLINE)

#if defined(_MSC_VER)
#define TB_FORCE_INLINE __forceinline
#elif defined(__clang__) || defined(__GNUC__)
// GCC/Clang prefer inline plus always_inline
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
#endif // TB_NO_FORCE_INLINE

// -----------------------------------------------------------------------------
// TB_NOINLINE

#if defined(_MSC_VER)
#define TB_NOINLINE __declspec(noinline)
#elif defined(__clang__) || defined(__GNUC__)
#if __has_attribute(noinline) || defined(__GNUC__)
#define TB_NOINLINE __attribute__((noinline))
#else
#define TB_NOINLINE
#endif
#else
#define TB_NOINLINE
#endif

// -----------------------------------------------------------------------------
// TB_RESTRICT

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

// -----------------------------------------------------------------------------
// TB_LIKELY / TB_UNLIKELY

#if defined(__clang__) || defined(__GNUC__)
#define TB_LIKELY(x) (__builtin_expect(!!(x), 1))
#define TB_UNLIKELY(x) (__builtin_expect(!!(x), 0))
#else
#define TB_LIKELY(x) (x)
#define TB_UNLIKELY(x) (x)
#endif

// -----------------------------------------------------------------------------
// TB_ASSUME

#if defined(_MSC_VER)
#define TB_ASSUME(x) __assume(x)
#elif defined(__clang__)
// Clang has __builtin_assume from about 3.8 onwards
#ifdef __has_builtin
#if __has_builtin(__builtin_assume)
#define TB_ASSUME(x) __builtin_assume(x)
#else
#define TB_ASSUME(x)                 \
    do                               \
    {                                \
        if (!(x))                    \
            __builtin_unreachable(); \
    } while (0)
#endif
#else
#define TB_ASSUME(x)                 \
    do                               \
    {                                \
        if (!(x))                    \
            __builtin_unreachable(); \
    } while (0)
#endif
#elif defined(__GNUC__)
#if (__GNUC__ >= 13) || (defined(__has_builtin) && __has_builtin(__builtin_assume))
#define TB_ASSUME(x) __builtin_assume(x)
#else
#define TB_ASSUME(x)                 \
    do                               \
    {                                \
        if (!(x))                    \
            __builtin_unreachable(); \
    } while (0)
#endif
#else
#define TB_ASSUME(x) ((void)0)
#endif

// -----------------------------------------------------------------------------
// TB_UNREACHABLE

#if defined(_MSC_VER)
#define TB_UNREACHABLE() __assume(0)
#elif defined(__clang__) || defined(__GNUC__)
#define TB_UNREACHABLE() __builtin_unreachable()
#else
#define TB_UNREACHABLE() ((void)0)
#endif
