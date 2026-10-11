#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <utility>

#include "simrv/xlen/Types.hpp"

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

constexpr auto to_trap_cause(ExceptionCode code) noexcept -> TrapCause {
    return static_cast<TrapCause>(code);
}

constexpr auto operator==(TrapCause cause, ExceptionCode code) noexcept -> bool {
    return cause == static_cast<TrapCause>(code);
}

constexpr auto operator==(ExceptionCode code, TrapCause cause) noexcept -> bool {
    return static_cast<TrapCause>(code) == cause;
}

constexpr auto operator!=(TrapCause cause, ExceptionCode code) noexcept -> bool {
    return !(cause == code);
}

constexpr auto operator!=(ExceptionCode code, TrapCause cause) noexcept -> bool {
    return !(code == cause);
}

/// High bit mask used to check if a trap cause represents an interrupt.
constexpr TrapCause kInterruptCauseBit = std::to_underlying(TrapFlag::Interrupt);
/// Bitmask extracting the exception code from a trap cause word.
constexpr TrapCause kExceptionCodeMask = static_cast<TrapCause>(kInterruptCauseBit - 1u);

/// Extract the exception code component from a trap cause value.
constexpr auto trap_exception_code(TrapCause cause) -> TrapCause {
    return cause & kExceptionCodeMask;
}

/// Convert an untyped or masked trap cause value to a typed ExceptionCode.
constexpr auto to_exception_code(TrapCause cause) noexcept -> ExceptionCode {
    return static_cast<ExceptionCode>(cause & kExceptionCodeMask);
}

/// Query whether a trap cause represents an asynchronous interrupt.
constexpr auto trap_is_interrupt(TrapCause cause) -> bool {
    return (cause & kInterruptCauseBit) != 0u;
}

/// Select the highest-priority pending standard interrupt per Privileged ISA 1.13 section 2.1.1.9.
constexpr auto select_highest_priority_interrupt(Word mask) -> Word {
    // Fixed priority: MEI -> MSI -> MTI -> SEI -> SSI -> STI. Platform interrupt
    // bits (16+) fall back to descending cause number, SimRV's documented custom ordering.
    constexpr std::array<Word, 6> kPriorityOrder = {11u, 3u, 7u, 9u, 1u, 5u};
    for (Word const irq : kPriorityOrder) {
        if ((mask & (1u << irq)) != 0u) {
            return irq;
        }
    }
    return static_cast<Word>(std::bit_width(mask) - 1u);
}

/// Shorthand constants for RISC-V privilege levels.
constexpr PrivilegeLevel kPrivUser = PrivilegeLevel::User;
constexpr PrivilegeLevel kPrivSupervisor = PrivilegeLevel::Supervisor;
constexpr PrivilegeLevel kPrivMachine = PrivilegeLevel::Machine;

[[nodiscard]] constexpr auto ecall_for_privilege(PrivilegeLevel priv) noexcept -> ExceptionCode {
    switch (priv) {
        case PrivilegeLevel::User:
            return ExceptionCode::UserEcall;
        case PrivilegeLevel::Supervisor:
            return ExceptionCode::SupervisorEcall;
        case PrivilegeLevel::Machine:
        default:
            return ExceptionCode::MachineEcall;
    }
}
