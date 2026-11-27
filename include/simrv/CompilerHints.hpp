/**
 * @file CompilerHints.hpp
 * @brief Compiler branch and inlining hints.
 */
#pragma once

namespace simrv::compiler {
template <typename T>
constexpr auto likely(const T& value) -> bool {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_expect(static_cast<bool>(value), true);
#else
    return static_cast<bool>(value);
#endif
}

template <typename T>
constexpr auto unlikely(const T& value) -> bool {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_expect(static_cast<bool>(value), false);
#else
    return static_cast<bool>(value);
#endif
}

inline void cpu_relax() noexcept {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#if defined(__GNUC__) || defined(__clang__)
    __builtin_ia32_pause();
#else
    _mm_pause();
#endif
#elif defined(__aarch64__) || defined(__arm__)
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("yield" ::: "memory");
#endif
#endif
}
}  // namespace simrv::compiler

#if defined(__GNUC__) || defined(__clang__)
#define SIMRV_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define SIMRV_ALWAYS_INLINE inline
#endif
