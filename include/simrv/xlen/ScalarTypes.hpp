/**
 * @file ScalarTypes.hpp
 * @brief XLEN scalar, architectural, and register identifier types.
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

// C++23 Conditional Type Selection
using Word = std::conditional_t<kIsXLen64, uint64_t, uint32_t>;
using SignedWord = std::conditional_t<kIsXLen64, int64_t, int32_t>;
using Register = Word;
using Byte = std::byte;
using CompressedInstruction = uint16_t;
using FloatingRegister = uint64_t;
using Counter = uint64_t;

// Forward declarations for strong semantic types
struct VirtAddr;
struct PhysAddr;
struct HartId;
struct CsrNumber;

// Architectural RISC-V Semantic Types
using Address = Word;
using Instruction = uint32_t;  // RISC-V base instructions are exactly 32 bits
using InstructionId = Counter;
using CycleCount = Counter;
using CSRValue = Word;
using CSRAddress = Address;
using ImmValue = SignedWord;
using TrapCause = Word;
using PageFaultCause = TrapCause;
using PortId = uint8_t;
using LatencyCycles = uint32_t;
using BhtIndex = uint32_t;
using BtbIndex = uint32_t;
using RasIndex = uint32_t;
using RasDepth = uint32_t;
using BranchTag = uint32_t;
using GlobalHistory = uint32_t;

enum class BranchDirection : uint8_t { NotTaken = 0, Taken = 1 };
using PageTableLevel = int;
using VpnIndex = uint32_t;
using PpnIndex = uint64_t;
using IrqNumber = uint32_t;
using InterruptSourceId = uint32_t;
using InterruptPriority = uint32_t;
using PlicContextId = uint32_t;
enum class IrqLevel : bool { Deasserted = false, Asserted = true };
using TimeBaseTicks = Counter;

using Funct7 = uint8_t;
using Funct12 = uint16_t;
using Funct6 = uint8_t;
using Funct2 = uint8_t;
using CsrImm = uint8_t;
enum class AccessWidth : uint8_t { Byte = 1, HalfWord = 2, Word = 4, DoubleWord = 8 };

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

}  // namespace simrv::xlen
