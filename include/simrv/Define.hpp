/**
 * @file Define.hpp
 * @brief Core ISA constants, compiler helpers, and simulator type forwarding.
 */
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "simrv/xlen/Constants.hpp"  // IWYU pragma: export
#include "simrv/xlen/Types.hpp"

#ifndef SIMRV_CORE_COUNT
#define SIMRV_CORE_COUNT 1
#endif
/// Number of simulated CPU cores.
inline constexpr unsigned kCoreCount = SIMRV_CORE_COUNT;

/// Maximum vector register length in bits.
inline constexpr unsigned kVlenMaxBits = 1024;
/// Maximum vector register length in bytes.
inline constexpr unsigned kVlenMaxBytes = kVlenMaxBits / 8;

using DumpFlags = uint8_t;

/// Flags controlling state logging and trace dumping.
enum class DumpFlag : DumpFlags {
    Exec = (1u << 0),
    Reg = (1u << 1),
    Csr = (1u << 2),
};

/// Page table levels for Sv32/Sv39 virtual memory translation.
constexpr uint32_t LEVELS = 2;
/// Page table entry size in bytes.
constexpr uint32_t PTE_SIZE = 4;
/// Standard page size in bytes (4 KiB).
constexpr uint32_t PAGE_SIZE = (1u << 12);

/// Bitmask for disk controller MMIO offset addressing.
constexpr Address DISK_MASK = static_cast<Address>(0x03ffffffu);

namespace simrv::compiler {
/// Branch prediction hint for likely true conditions.
template <typename T>
constexpr auto likely(T value) -> bool {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_expect(static_cast<bool>(value), true);
#else
    return static_cast<bool>(value);
#endif
}

/// Branch prediction hint for unlikely true conditions.
template <typename T>
constexpr auto unlikely(T value) -> bool {
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

/// Flag bit distinguishing interrupts from synchronous exceptions.
enum class TrapFlag : TrapCause {
    Interrupt = static_cast<TrapCause>(Word{1} << (simrv::xlen::kXLenBits - 1u)),
};

/// Standard RISC-V exception cause codes.
enum class ExceptionCode : uint8_t {
    MisalignedFetch = 0x0,
    FaultFetch = 0x1,
    IllegalInstruction = 0x2,
    Breakpoint = 0x3,
    MisalignedLoad = 0x4,
    FaultLoad = 0x5,
    MisalignedStore = 0x6,
    FaultStore = 0x7,
    UserEcall = 0x8,
    SupervisorEcall = 0x9,
    HypervisorEcall = 0xa,
    MachineEcall = 0xb,
    FetchPageFault = 0xc,
    LoadPageFault = 0xd,
    StorePageFault = 0xf,
};

/// High bit mask used to check if a trap cause represents an interrupt.
constexpr TrapCause kInterruptCauseBit = enum_mask(TrapFlag::Interrupt);
/// Bitmask extracting the exception code from a trap cause word.
constexpr TrapCause kExceptionCodeMask = static_cast<TrapCause>(kInterruptCauseBit - 1u);

/// Extract the exception code component from a trap cause value.
constexpr auto trap_exception_code(TrapCause cause) -> TrapCause {
    return cause & kExceptionCodeMask;
}

/// Query whether a trap cause represents an asynchronous interrupt.
constexpr auto trap_is_interrupt(TrapCause cause) -> bool {
    return (cause & kInterruptCauseBit) != 0u;
}

/// Shorthand constants for RISC-V privilege levels.
constexpr PrivilegeLevel kPrivUser = PrivilegeLevel::User;
constexpr PrivilegeLevel kPrivSupervisor = PrivilegeLevel::Supervisor;
constexpr PrivilegeLevel kPrivMachine = PrivilegeLevel::Machine;

// Include split domain headers
#include "simrv/core/CsrTypes.hpp"  // IWYU pragma: export
#include "simrv/device/Virtio.hpp"  // IWYU pragma: export
#include "simrv/isa/Common.hpp"     // IWYU pragma: export
#include "simrv/memory/Mmu.hpp"     // IWYU pragma: export
