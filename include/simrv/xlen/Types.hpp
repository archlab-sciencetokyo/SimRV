/**
 * @file Types.hpp
 * @brief Base types and widths for the XLEN abstraction layer.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#ifndef SIMRV_XLEN
#define SIMRV_XLEN 32
#endif

static_assert(SIMRV_XLEN == 32 || SIMRV_XLEN == 64, "SIMRV_XLEN must be 32 or 64");

namespace simrv::xlen {
inline constexpr bool kIsXLen64 = (SIMRV_XLEN == 64);
inline constexpr unsigned kXLenBits = SIMRV_XLEN;
inline constexpr unsigned kXLenHexDigits = kXLenBits / 4u;
}  // namespace simrv::xlen

// Expose fundamental constants for global compatibility
using simrv::xlen::kIsXLen64;
using simrv::xlen::kXLenBits;
using simrv::xlen::kXLenHexDigits;

// C++23 Conditional Type Selection
using Word = std::conditional_t<kIsXLen64, uint64_t, uint32_t>;
using SignedWord = std::conditional_t<kIsXLen64, int64_t, int32_t>;
using Register = Word;
using Byte = std::byte;
using CompressedInstruction = uint16_t;
using FloatingRegister = uint64_t;
using Counter = uint64_t;

// Architectural RISC-V Semantic Aliases
using Address = Word;
using PhysAddr = Address;
using VirtAddr = Address;
using Instruction = uint32_t;  // RISC-V base instructions are exactly 32 bits
using CSRValue = Word;
using CSRAddress = Address;
using ImmValue = SignedWord;
using TrapCause = Word;
enum class PrivilegeLevel : uint8_t { User = 0, Supervisor = 1, Machine = 3 };

constexpr auto operator<(PrivilegeLevel lhs, PrivilegeLevel rhs) -> bool {
    return std::to_underlying(lhs) < std::to_underlying(rhs);
}

constexpr auto operator<=(PrivilegeLevel lhs, PrivilegeLevel rhs) -> bool {
    return std::to_underlying(lhs) <= std::to_underlying(rhs);
}

constexpr auto operator>(PrivilegeLevel lhs, PrivilegeLevel rhs) -> bool {
    return std::to_underlying(lhs) > std::to_underlying(rhs);
}

constexpr auto operator>=(PrivilegeLevel lhs, PrivilegeLevel rhs) -> bool {
    return std::to_underlying(lhs) >= std::to_underlying(rhs);
}

enum class RegId : uint8_t {
    Zero = 0,
    Ra = 1,
    Sp = 2,
    Gp = 3,
    Tp = 4,
    T0 = 5,
    T1 = 6,
    T2 = 7,
    S0 = 8,
    Fp = 8,
    S1 = 9,
    A0 = 10,
    A1 = 11,
    A2 = 12,
    A3 = 13,
    A4 = 14,
    A5 = 15,
    A6 = 16,
    A7 = 17,
    S2 = 18,
    S3 = 19,
    S4 = 20,
    S5 = 21,
    S6 = 22,
    S7 = 23,
    S8 = 24,
    S9 = 25,
    S10 = 26,
    S11 = 27,
    T3 = 28,
    T4 = 29,
    T5 = 30,
    T6 = 31
};

enum class FpRegId : uint8_t {
    Ft0 = 0,
    Ft1 = 1,
    Ft2 = 2,
    Ft3 = 3,
    Ft4 = 4,
    Ft5 = 5,
    Ft6 = 6,
    Ft7 = 7,
    Fs0 = 8,
    Fs1 = 9,
    Fa0 = 10,
    Fa1 = 11,
    Fa2 = 12,
    Fa3 = 13,
    Fa4 = 14,
    Fa5 = 15,
    Fa6 = 16,
    Fa7 = 17,
    Fs2 = 18,
    Fs3 = 19,
    Fs4 = 20,
    Fs5 = 21,
    Fs6 = 22,
    Fs7 = 23,
    Fs8 = 24,
    Fs9 = 25,
    Fs10 = 26,
    Fs11 = 27,
    Ft8 = 28,
    Ft9 = 29,
    Ft10 = 30,
    Ft11 = 31
};

template <typename EnumType>
constexpr auto enum_mask(EnumType bit) {
    return std::to_underlying(bit);
}

template <typename EnumType>
constexpr bool has_enum_mask(std::underlying_type_t<EnumType> value, EnumType bit) {
    return (value & enum_mask(bit)) != 0;
}