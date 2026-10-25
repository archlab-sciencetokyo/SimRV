#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <utility>

#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::isa {

/// Standard RISC-V interrupt cause codes per Privileged ISA 1.13.
enum class InterruptCode : uint8_t {
    SupervisorSoftware = 1,
    MachineSoftware = 3,
    SupervisorTimer = 5,
    MachineTimer = 7,
    SupervisorExternal = 9,
    MachineExternal = 11,
    SupervisorGuestExternal = 12,
    CounterOverflow = 13,
};

/// Flag bit distinguishing interrupts from synchronous exceptions.
enum class TrapFlag : simrv::xlen::TrapCause {
    Interrupt =
        static_cast<simrv::xlen::TrapCause>(simrv::xlen::Word{1} << (simrv::xlen::kXLenBits - 1u)),
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
    SoftwareCheck = 0x12,  ///< Software check / pointer checking (cause 18, Privileged ISA 1.13)
    HardwareError = 0x13,  ///< Hardware error (cause 19, Privileged ISA 1.13)
};

constexpr auto to_trap_cause(ExceptionCode code) noexcept -> simrv::xlen::TrapCause {
    return static_cast<simrv::xlen::TrapCause>(code);
}

constexpr auto to_trap_cause(InterruptCode code) noexcept -> simrv::xlen::TrapCause {
    return std::to_underlying(TrapFlag::Interrupt) | static_cast<simrv::xlen::TrapCause>(code);
}

constexpr auto operator==(simrv::xlen::TrapCause cause, ExceptionCode code) noexcept -> bool {
    return cause == static_cast<simrv::xlen::TrapCause>(code);
}

constexpr auto operator==(ExceptionCode code, simrv::xlen::TrapCause cause) noexcept -> bool {
    return static_cast<simrv::xlen::TrapCause>(code) == cause;
}

constexpr auto operator!=(simrv::xlen::TrapCause cause, ExceptionCode code) noexcept -> bool {
    return !(cause == code);
}

constexpr auto operator!=(ExceptionCode code, simrv::xlen::TrapCause cause) noexcept -> bool {
    return !(code == cause);
}

constexpr auto operator==(simrv::xlen::TrapCause cause, InterruptCode code) noexcept -> bool {
    return cause == to_trap_cause(code);
}

constexpr auto operator==(InterruptCode code, simrv::xlen::TrapCause cause) noexcept -> bool {
    return to_trap_cause(code) == cause;
}

constexpr auto operator!=(simrv::xlen::TrapCause cause, InterruptCode code) noexcept -> bool {
    return !(cause == code);
}

constexpr auto operator!=(InterruptCode code, simrv::xlen::TrapCause cause) noexcept -> bool {
    return !(code == cause);
}

/// High bit mask used to check if a trap cause represents an interrupt.
constexpr simrv::xlen::TrapCause kInterruptCauseBit = std::to_underlying(TrapFlag::Interrupt);
/// Bitmask extracting the exception code from a trap cause word.
constexpr simrv::xlen::TrapCause kExceptionCodeMask =
    static_cast<simrv::xlen::TrapCause>(kInterruptCauseBit - 1u);

/// Extract the exception code component from a trap cause value.
constexpr auto trap_exception_code(simrv::xlen::TrapCause cause) -> simrv::xlen::TrapCause {
    return cause & kExceptionCodeMask;
}

/// Convert an untyped or masked trap cause value to a typed ExceptionCode.
constexpr auto to_exception_code(simrv::xlen::TrapCause cause) noexcept -> ExceptionCode {
    return static_cast<ExceptionCode>(cause & kExceptionCodeMask);
}

/// Convert an untyped or masked interrupt cause value to a typed InterruptCode.
constexpr auto to_interrupt_code(simrv::xlen::TrapCause cause) noexcept -> InterruptCode {
    return static_cast<InterruptCode>(cause & kExceptionCodeMask);
}

/// Query whether a trap cause represents an asynchronous interrupt.
constexpr auto trap_is_interrupt(simrv::xlen::TrapCause cause) -> bool {
    return (cause & kInterruptCauseBit) != 0u;
}

/// Select the highest-priority pending standard interrupt per Privileged ISA 1.13 section 2.1.1.9.
constexpr auto select_highest_priority_interrupt(simrv::xlen::Word mask) -> simrv::xlen::Word {
    // Fixed priority: MEI -> MSI -> MTI -> SEI -> SSI -> STI -> LCOFI. Platform interrupt
    // bits (16+) fall back to descending cause number, SimRV's documented custom ordering.
    constexpr std::array<InterruptCode, 7> kPriorityOrder = {
        InterruptCode::MachineExternal,    InterruptCode::MachineSoftware,
        InterruptCode::MachineTimer,       InterruptCode::SupervisorExternal,
        InterruptCode::SupervisorSoftware, InterruptCode::SupervisorTimer,
        InterruptCode::CounterOverflow,
    };
    for (InterruptCode const irq : kPriorityOrder) {
        auto const shift = std::to_underlying(irq);
        if ((mask & (simrv::xlen::Word{1} << shift)) != 0u) {
            return shift;
        }
    }
    return static_cast<simrv::xlen::Word>(std::bit_width(mask) - 1u);
}

/// Shorthand constants for RISC-V privilege levels.
constexpr simrv::xlen::PrivilegeLevel kPrivUser = simrv::xlen::PrivilegeLevel::User;
constexpr simrv::xlen::PrivilegeLevel kPrivSupervisor = simrv::xlen::PrivilegeLevel::Supervisor;
constexpr simrv::xlen::PrivilegeLevel kPrivMachine = simrv::xlen::PrivilegeLevel::Machine;

[[nodiscard]] constexpr auto ecall_for_privilege(simrv::xlen::PrivilegeLevel priv) noexcept
    -> ExceptionCode {
    switch (priv) {
        case simrv::xlen::PrivilegeLevel::User:
            return ExceptionCode::UserEcall;
        case simrv::xlen::PrivilegeLevel::Supervisor:
            return ExceptionCode::SupervisorEcall;
        case simrv::xlen::PrivilegeLevel::Machine:
        default:
            return ExceptionCode::MachineEcall;
    }
}

/// Return the relative priority rank of a synchronous exception per Privileged ISA 1.13 Table 3.7.
/// Lower integer value corresponds to higher architectural priority (1 is highest).
[[nodiscard]] constexpr auto exception_priority_rank(ExceptionCode code) noexcept -> uint8_t {
    switch (code) {
        case ExceptionCode::MisalignedFetch:
            return 1;
        case ExceptionCode::FaultFetch:
        case ExceptionCode::FetchPageFault:
            return 2;
        case ExceptionCode::IllegalInstruction:
            return 3;
        case ExceptionCode::Breakpoint:
        case ExceptionCode::UserEcall:
        case ExceptionCode::SupervisorEcall:
        case ExceptionCode::HypervisorEcall:
        case ExceptionCode::MachineEcall:
            return 4;
        case ExceptionCode::MisalignedStore:
        case ExceptionCode::MisalignedLoad:
            return 5;
        case ExceptionCode::FaultStore:
        case ExceptionCode::StorePageFault:
        case ExceptionCode::FaultLoad:
        case ExceptionCode::LoadPageFault:
            return 6;
        case ExceptionCode::SoftwareCheck:
        case ExceptionCode::HardwareError:
            return 7;
        default:
            return 255;
    }
}

/// Select the higher-priority synchronous exception between two candidate causes per Privileged
/// ISA 1.13.
[[nodiscard]] constexpr auto higher_priority_exception(ExceptionCode a, ExceptionCode b) noexcept
    -> ExceptionCode {
    return exception_priority_rank(a) <= exception_priority_rank(b) ? a : b;
}

}  // namespace simrv::isa

// Global compatibility aliases per repository guidelines
using simrv::isa::ecall_for_privilege;
using simrv::isa::exception_priority_rank;
using simrv::isa::ExceptionCode;
using simrv::isa::higher_priority_exception;
using simrv::isa::InterruptCode;
using simrv::isa::kExceptionCodeMask;
using simrv::isa::kInterruptCauseBit;
using simrv::isa::kPrivMachine;
using simrv::isa::kPrivSupervisor;
using simrv::isa::kPrivUser;
using simrv::isa::select_highest_priority_interrupt;
using simrv::isa::to_exception_code;
using simrv::isa::to_interrupt_code;
using simrv::isa::to_trap_cause;
using simrv::isa::trap_exception_code;
using simrv::isa::trap_is_interrupt;
using simrv::isa::TrapFlag;
