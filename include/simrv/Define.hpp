/**
 * @file Define.hpp
 * @brief Core ISA constants, compiler helpers, and simulator type forwarding.
 */
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "simrv/xlen/Constants.hpp" // IWYU pragma: export
#include "simrv/xlen/Types.hpp"

#ifndef SIMRV_CORE_COUNT
#define SIMRV_CORE_COUNT 1
#endif
inline constexpr unsigned kCoreCount = SIMRV_CORE_COUNT;

inline constexpr unsigned kVlenMaxBits = 1024;
inline constexpr unsigned kVlenMaxBytes = kVlenMaxBits / 8;

using DumpFlags = uint8_t;

enum class DumpFlag : DumpFlags {
    Exec = (1u << 0),
    Reg = (1u << 1),
    Csr = (1u << 2),
};

constexpr uint32_t LEVELS = 2;
constexpr uint32_t PTE_SIZE = 4;
constexpr uint32_t PAGE_SIZE = (1u << 12);

constexpr Address DISK_MASK = static_cast<Address>(0x03ffffffu);

namespace simrv::compiler {
template <typename T>
constexpr auto likely(T value) -> bool {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_expect(static_cast<bool>(value), true);
#else
    return static_cast<bool>(value);
#endif
}

template <typename T>
constexpr auto unlikely(T value) -> bool {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_expect(static_cast<bool>(value), false);
#else
    return static_cast<bool>(value);
#endif
}
}  // namespace simrv::compiler

enum class TrapFlag : TrapCause {
    Interrupt = static_cast<TrapCause>(Word{1} << (simrv::xlen::kXLenBits - 1u)),
};

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

constexpr TrapCause kInterruptCauseBit = enum_mask(TrapFlag::Interrupt);
constexpr TrapCause kExceptionCodeMask = static_cast<TrapCause>(kInterruptCauseBit - 1u);

constexpr auto trap_exception_code(TrapCause cause) -> TrapCause {
    return cause & kExceptionCodeMask;
}

constexpr auto trap_is_interrupt(TrapCause cause) -> bool {
    return (cause & kInterruptCauseBit) != 0u;
}

constexpr PrivilegeLevel kPrivUser = PrivilegeLevel::User;
constexpr PrivilegeLevel kPrivSupervisor = PrivilegeLevel::Supervisor;
constexpr PrivilegeLevel kPrivMachine = PrivilegeLevel::Machine;

// Include split domain headers
#include "simrv/core/CsrTypes.hpp" // IWYU pragma: export
#include "simrv/isa/Common.hpp" // IWYU pragma: export
#include "simrv/memory/Mmu.hpp" // IWYU pragma: export
#include "simrv/device/Virtio.hpp" // IWYU pragma: export
