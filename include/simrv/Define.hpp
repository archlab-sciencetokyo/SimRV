/**
 * @file Define.hpp
 * @brief Core ISA constants, compiler helpers, and simulator type forwarding.
 */
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "simrv/isa/TrapTypes.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"

#ifndef SIMRV_DISK_SIZE_MB
#define SIMRV_DISK_SIZE_MB 256
#endif

#ifndef SIMRV_DRAM_SIZE_MB
#define SIMRV_DRAM_SIZE_MB 256
#endif

/// Maximum vector register length in bits.
inline constexpr unsigned kVlenMaxBits = 1024;
/// Maximum vector register length in bytes.
inline constexpr unsigned kVlenMaxBytes = kVlenMaxBits / 8;

namespace simrv::compiler {
/// Branch prediction hint for likely true conditions.
template <typename T>
constexpr auto likely(const T& value) -> bool {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_expect(static_cast<bool>(value), true);
#else
    return static_cast<bool>(value);
#endif
}

/// Branch prediction hint for unlikely true conditions.
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
