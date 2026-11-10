/**
 * @file TrapDescriptor.hpp
 * @brief Structured, typesafe representation of RISC-V architectural traps and dispositions.
 */
#pragma once

#include <cstdint>
#include <string>

#include "simrv/isa/TrapTypes.hpp"
#include "simrv/xlen/AddressTypes.hpp"

namespace simrv::core {

enum class TrapCategory : std::uint8_t {
    Interrupt,
    EnvironmentCall,
    Breakpoint,
    InstructionFault,
    DataFault,
    IllegalInstruction,
    HardwareError,
    VirtualizationFault,
    Unknown,
};

enum class TrapDisposition : std::uint8_t {
    Delivered,         ///< Successfully dispatched and vectored into guest handler
    HandledByHost,     ///< Intercepted and handled by host runtime (e.g. SBI call)
    PausedByDebugger,  ///< Breakpoint / trigger paused for interactive inspection
    Unrecoverable,     ///< Zero vector, recursive loop, or unhandled hardware fault
};

struct TrapDescriptor {
    simrv::xlen::TrapCause cause{0};
    simrv::xlen::VirtAddr origin_pc{};
    simrv::xlen::CsrValue tval{0};
    simrv::xlen::CsrValue htval{0};
    simrv::xlen::CsrValue htinst{0};
    simrv::xlen::PrivilegeLevel origin_priv{simrv::xlen::PrivilegeLevel::Machine};
    bool origin_virt{false};

    [[nodiscard]] constexpr auto is_interrupt() const noexcept -> bool {
        return (cause & isa::kInterruptCauseBit) != 0;
    }

    [[nodiscard]] constexpr auto exception_code() const noexcept -> simrv::isa::ExceptionCode {
        return static_cast<simrv::isa::ExceptionCode>(cause & ~isa::kInterruptCauseBit);
    }

    [[nodiscard]] constexpr auto interrupt_code() const noexcept -> simrv::isa::InterruptCode {
        return static_cast<simrv::isa::InterruptCode>(cause & ~isa::kInterruptCauseBit);
    }

    [[nodiscard]] auto category() const noexcept -> TrapCategory {
        if (is_interrupt()) return TrapCategory::Interrupt;

        using simrv::isa::ExceptionCode;
        switch (exception_code()) {
            case ExceptionCode::UserEcall:
            case ExceptionCode::SupervisorEcall:
            case ExceptionCode::HypervisorEcall:
            case ExceptionCode::MachineEcall:
                return TrapCategory::EnvironmentCall;

            case ExceptionCode::Breakpoint:
                return TrapCategory::Breakpoint;

            case ExceptionCode::MisalignedFetch:
            case ExceptionCode::FaultFetch:
            case ExceptionCode::FetchPageFault:
            case ExceptionCode::GuestInstructionPageFault:
                return TrapCategory::InstructionFault;

            case ExceptionCode::MisalignedLoad:
            case ExceptionCode::FaultLoad:
            case ExceptionCode::MisalignedStore:
            case ExceptionCode::FaultStore:
            case ExceptionCode::LoadPageFault:
            case ExceptionCode::StorePageFault:
            case ExceptionCode::GuestLoadPageFault:
            case ExceptionCode::GuestStorePageFault:
                return TrapCategory::DataFault;

            case ExceptionCode::IllegalInstruction:
                return TrapCategory::IllegalInstruction;

            case ExceptionCode::VirtualInstruction:
                return TrapCategory::VirtualizationFault;

            case ExceptionCode::HardwareError:
                return TrapCategory::HardwareError;

            case ExceptionCode::SoftwareCheck:
                return TrapCategory::InstructionFault;

            default:
                return TrapCategory::Unknown;
        }
    }

    [[nodiscard]] constexpr auto is_fatal() const noexcept -> bool {
        return !is_interrupt() && exception_code() == simrv::isa::ExceptionCode::HardwareError;
    }
};

}  // namespace simrv::core
