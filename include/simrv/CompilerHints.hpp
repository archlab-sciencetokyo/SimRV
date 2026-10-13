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
}  // namespace simrv::compiler

#if defined(__GNUC__) || defined(__clang__)
#define SIMRV_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define SIMRV_ALWAYS_INLINE inline
#endif
