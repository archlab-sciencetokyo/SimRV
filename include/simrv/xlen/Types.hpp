/**
 * @file Types.hpp
 * @brief Base types and widths for the XLEN abstraction layer.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

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
using Instruction = uint32_t;  // RISC-V base instructions are exactly 32 bits
using CSRValue = Word;
using CSRAddress = Address;
using ImmValue = SignedWord;
using TrapCause = Word;
using PrivilegeLevel = uint8_t;