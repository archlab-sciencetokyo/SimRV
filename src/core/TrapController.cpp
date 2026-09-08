/**
 * @file TrapController.cpp
 * @brief RISC-V architectural trap vectoring, exception escalation, and privilege management.
 */
#include "simrv/core/TrapController.hpp"

#include <cstdint>
#include <print>
#include <string>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/Sbi.hpp"
#include "simrv/core/TelemetrySink.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Helpers.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

namespace {

constexpr int kLogHexWidth = static_cast<int>(kXLenHexDigits);

auto trap_cause_name(TrapCause cause) -> std::string {
    const bool is_interrupt = trap_is_interrupt(cause);
    const uint64_t code = trap_exception_code(cause);
    if (is_interrupt) {
        switch (code) {
            case 1:
                return "Supervisor software interrupt";
            case 3:
                return "Machine software interrupt";
            case 5:
                return "Supervisor timer interrupt";
            case 7:
                return "Machine timer interrupt";
            case 9:
                return "Supervisor external interrupt";
            case 11:
                return "Machine external interrupt";
            default:
                return "Unknown interrupt " + std::to_string(code);
        }
    } else {
        switch (code) {
            case 0:
                return "Instruction address misaligned";
            case 1:
                return "Instruction access fault";
            case 2:
                return "Illegal instruction";
            case 3:
                return "Breakpoint";
            case 4:
                return "Load address misaligned";
            case 5:
                return "Load access fault";
            case 6:
                return "Store/AMO address misaligned";
            case 7:
                return "Store/AMO access fault";
            case 8:
                return "Environment call from U-mode";
            case 9:
                return "Environment call from S-mode";
            case 11:
                return "Environment call from M-mode";
            case 12:
                return "Instruction page fault";
            case 13:
                return "Load page fault";
            case 15:
                return "Store/AMO page fault";
            default:
                return "Unknown exception " + std::to_string(code);
        }
    }
}

[[nodiscard]] constexpr auto calculate_trap_target(CSRValue tvec, TrapCause cause) noexcept
    -> Address {
    const Address tvec_base = tvec & ~Address{3};
    const Word tvec_mode = tvec & 3;
    if (tvec_mode == 1 && trap_is_interrupt(cause)) {
        return tvec_base + 4 * trap_exception_code(cause);
    }
    return tvec_base;
}

}  // namespace

void TrapController::mret(ArchState& state) {
    CSRValue mstatus = state.mstatus;
    const CSRValue mpp = (mstatus & enum_mask(MstatusBit::Mpp)) >> 11;
    const CSRValue mpie = (mstatus & enum_mask(MstatusBit::Mpie)) >> 7;

    // Restore MIE (bit 3) from MPIE (bit 7)
    if (mpie != 0) {
        mstatus |= enum_mask(MstatusBit::Mie);
    } else {
        mstatus &= ~enum_mask(MstatusBit::Mie);
    }
    // Set MPIE (bit 7) to 1
    mstatus |= enum_mask(MstatusBit::Mpie);
    // xRET resets xPP to the least-privileged supported mode.
    const bool has_s = isa::misa_has_extension(state.misa, isa::IsaExtension::S);
    const bool has_u = isa::misa_has_extension(state.misa, isa::IsaExtension::U);
    mstatus = (mstatus & ~enum_mask(MstatusBit::Mpp)) | (least_supported_mpp(has_s, has_u) << 11U);

    if (static_cast<PrivilegeLevel>(mpp) < kPrivMachine) {
        mstatus &= ~enum_mask(MstatusBit::Mprv);
    }

    state.mstatus = mstatus;
    state.priv = static_cast<PrivilegeLevel>(mpp);
    state.update_xlen();
    state.pc = isa::epc_read_value(state.mepc, state.misa);
    if (state.regs.xlen == 32) {
        state.pc = static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(state.pc)));
    }
    state.reserved = 0;
}

void TrapController::sret(ArchState& state) {
    CSRValue mstatus = state.mstatus;
    const CSRValue spp = (mstatus & enum_mask(MstatusBit::Spp)) >> 8;
    const CSRValue spie = (mstatus & enum_mask(MstatusBit::Spie)) >> 5;

    // Restore SIE (bit 1) from SPIE (bit 5)
    if (spie != 0) {
        mstatus |= enum_mask(MstatusBit::Sie);
    } else {
        mstatus &= ~enum_mask(MstatusBit::Sie);
    }
    // Set SPIE (bit 5) to 1
    mstatus |= enum_mask(MstatusBit::Spie);
    // Clear SPP (bit 8) to U-mode (0)
    mstatus &= ~enum_mask(MstatusBit::Spp);

    // Returning to a less privileged mode than M-mode always clears MPRV
    mstatus &= ~enum_mask(MstatusBit::Mprv);

    state.mstatus = mstatus;
    state.priv = static_cast<PrivilegeLevel>(spp);
    state.update_xlen();
    state.pc = isa::epc_read_value(state.sepc, state.misa);
    if (state.regs.xlen == 32) {
        state.pc = static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(state.pc)));
    }
    state.reserved = 0;
}

void TrapController::raise_exception(CPU& cpu, TrapCause cause, CSRValue tval) {
    ArchState& state = cpu.state();
    const Address trap_pc = state.pc;

    if (cpu.trap_log_stream != nullptr && cpu.trap_log_stream->is_open()) {
        std::println(
            *cpu.trap_log_stream,
            "TRAP mtime={} cause={:0{}x} ({}) pc={:0{}x} priv={} ra={:0{}x} sp={:0{}x} tp={:0{}x} "
            "a0={:0{}x} "
            "a1={:0{}x} mtvec={:0{}x} stvec={:0{}x} mepc={:0{}x} sepc={:0{}x} satp={:0{}x} "
            "tval={:0{}x}",
            static_cast<Counter>(cpu.clint_mmio.mtime.load()), static_cast<uint64_t>(cause),
            kLogHexWidth, trap_cause_name(cause), static_cast<uint64_t>(trap_pc), kLogHexWidth,
            static_cast<unsigned>(state.priv), static_cast<uint64_t>(state.regs.read(RegId::Ra)),
            kLogHexWidth, static_cast<uint64_t>(state.regs.read(RegId::Sp)), kLogHexWidth,
            static_cast<uint64_t>(state.regs.read(RegId::Tp)), kLogHexWidth,
            static_cast<uint64_t>(state.regs.read(RegId::A0)), kLogHexWidth,
            static_cast<uint64_t>(state.regs.read(RegId::A1)), kLogHexWidth,
            static_cast<uint64_t>(state.mtvec), kLogHexWidth, static_cast<uint64_t>(state.stvec),
            kLogHexWidth, static_cast<uint64_t>(state.mepc), kLogHexWidth,
            static_cast<uint64_t>(state.sepc), kLogHexWidth, static_cast<uint64_t>(state.satp),
            kLogHexWidth, static_cast<uint64_t>(tval), kLogHexWidth);
        cpu.trap_log_stream->flush();
    }

    if (cpu.sbi.handle_ecall(cause)) {
        state.reserved = 0;
        cpu.pipeline_context.pending_exception = std::nullopt;
        cpu.pipeline_context.pending_tval = 0;
        return;
    }

    CSRValue deleg = 0;
    if (state.priv <= kPrivSupervisor && misa_has_extension(state.misa, isa::IsaExtension::S)) {
        const auto cause_code = static_cast<CSRValue>(trap_exception_code(cause));
        if (cause_code < xlen::kXLenBits) {
            if (trap_is_interrupt(cause)) {
                deleg = (state.mideleg >> cause_code) & 1;
            } else {
                deleg = (state.medeleg >> cause_code) & 1;
            }
        }
    } else {
        deleg = 0;
    }

    if (deleg != 0u) {
        state.scause = cause;
        state.sepc = trap_pc;
        state.stval = tval;
        state.mstatus = (state.mstatus & ~enum_mask(MstatusBit::Spie)) |
                        (((state.mstatus & enum_mask(MstatusBit::Sie)) >> 1) << 5);
        state.mstatus = (state.mstatus & ~enum_mask(MstatusBit::Spp)) |
                        (static_cast<CSRValue>(std::to_underlying(state.priv)) << 8);
        state.mstatus &= ~enum_mask(MstatusBit::Sie);
        state.priv = kPrivSupervisor;
        state.update_xlen();
        state.pc = calculate_trap_target(state.stvec, cause);
    } else {
        state.mcause = cause;
        state.mepc = trap_pc;
        state.mtval = tval;
        state.mstatus = (state.mstatus & ~enum_mask(MstatusBit::Mpie)) |
                        (((state.mstatus & enum_mask(MstatusBit::Mie)) >> 3) << 7);
        state.mstatus = (state.mstatus & ~enum_mask(MstatusBit::Mpp)) |
                        (static_cast<CSRValue>(std::to_underlying(state.priv)) << 11);
        state.mstatus &= ~enum_mask(MstatusBit::Mie);
        state.priv = kPrivMachine;
        state.update_xlen();
        state.pc = calculate_trap_target(state.mtvec, cause);
    }
    if (state.regs.xlen == 32) {
        state.pc = static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(state.pc)));
    }
    // A delivered trap resumes through its guest handler.  The simulator only stops when no
    // handler exists; debugger breakpoints and watchpoints pause independently in CPU::run_cycle.
    if (state.pc == 0) {
        simrv::log::error(
            "[FATAL] Trap vector (mtvec/stvec) is 0. Cannot handle trap: {} (0x{:x}) at PC 0x{:x}. "
            "Halting simulator.",
            trap_cause_name(cause), static_cast<uint64_t>(cause), static_cast<uint64_t>(trap_pc));
        if (cpu.machine_) {
            if (auto sink = cpu.machine_->telemetry_sink()) {
                sink->set_persistent_status_override("\033[1;31m UNHANDLED TRAP \033[0m");
                sink->pause_loop();
            }
            cpu.machine_->stop(Machine::StopReason::UnhandledTrap);
        }
    }
    state.reserved = 0;
    cpu.pipeline_context.pending_exception = std::nullopt;
    cpu.pipeline_context.pending_tval = 0;
}

auto TrapController::can_execute_privileged_instruction(PrivilegeLevel current_priv, CSRValue misa,
                                                        CSRValue mstatus, Instruction funct12,
                                                        Word funct7) -> bool {
    if (funct12 == static_cast<Instruction>(isa::Funct12Priv::Mret)) {
        return current_priv >= kPrivMachine;
    }
    if (funct12 == static_cast<Instruction>(isa::Funct12Priv::Sret)) {
        return misa_has_extension(misa, isa::IsaExtension::S) && current_priv >= kPrivSupervisor &&
               !(current_priv == kPrivSupervisor && (mstatus & enum_mask(MstatusBit::Tsr)) != 0);
    }
    if (funct12 == static_cast<Instruction>(isa::Funct12Priv::Uret)) {
        return false;
    }
    if (funct12 == static_cast<Instruction>(isa::Funct12Priv::Wfi)) {
        return current_priv >= kPrivSupervisor &&
               !(current_priv == kPrivSupervisor && (mstatus & enum_mask(MstatusBit::Tw)) != 0);
    }
    if (funct7 == static_cast<Instruction>(isa::Funct7Priv::SfenceVma)) {
        return misa_has_extension(misa, isa::IsaExtension::S) && current_priv >= kPrivSupervisor &&
               !(current_priv == kPrivSupervisor && (mstatus & enum_mask(MstatusBit::Tvm)) != 0);
    }
    return true;
}

auto TrapController::can_access_csr(PrivilegeLevel current_priv, CSRValue misa, CSRAddress csr_addr,
                                    bool is_write) -> bool {
    // Debug CSRs require architectural Debug Mode, which the guest execution
    // engine does not implement. M-mode also cannot override CSR nonexistence.
    return csr_access_permitted(current_priv, isa::misa_has_extension(misa, isa::IsaExtension::S),
                                isa::misa_has_extension(misa, isa::IsaExtension::U), csr_addr,
                                is_write);
}

}  // namespace simrv::core
