/**
 * @file TrapController.cpp
 * @brief RISC-V architectural trap vectoring, exception escalation, and privilege management.
 */
#include "simrv/core/TrapController.hpp"

#include <cstdint>
#include <print>
#include <string>

#include "simrv/CompilerHints.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/Sbi.hpp"
#include "simrv/core/TelemetrySink.hpp"
#include "simrv/core/Tracer.hpp"
#include "simrv/isa/Priv.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Helpers.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

namespace {

[[nodiscard]] constexpr auto calculate_trap_target(CSRValue tvec, TrapCause cause) noexcept
    -> Address {
    const Address tvec_base = tvec & ~Address{3};
    const Word tvec_mode = tvec & 3;
    if (tvec_mode == 1 && trap_is_interrupt(cause)) {
        return tvec_base + 4 * trap_exception_code(cause);
    }
    return tvec_base;
}

auto trap_delegated_to_supervisor(const ArchState& state, TrapCause cause) -> bool {
    if (state.priv > kPrivSupervisor || !misa_has_extension(state.misa, isa::IsaExtension::S)) {
        return false;
    }
    const auto cause_code = static_cast<CSRValue>(trap_exception_code(cause));
    if (cause_code >= xlen::kXLenBits) return false;
    const CSRValue delegation = trap_is_interrupt(cause) ? state.mideleg : state.medeleg;
    return ((delegation >> cause_code) & 1U) != 0;
}

void stack_trap_interrupt_state(ArchState& state, MstatusBit interrupt_enable,
                                MstatusBit previous_interrupt_enable) {
    if ((state.mstatus & interrupt_enable) != 0) {
        state.mstatus |= previous_interrupt_enable;
    } else {
        state.mstatus &= ~static_cast<CSRValue>(previous_interrupt_enable);
    }
    state.mstatus &= ~static_cast<CSRValue>(interrupt_enable);
}

void restore_trap_interrupt_state(CSRValue& mstatus, MstatusBit interrupt_enable,
                                  MstatusBit previous_interrupt_enable) {
    if ((mstatus & previous_interrupt_enable) != 0) {
        mstatus |= interrupt_enable;
    } else {
        mstatus &= ~static_cast<CSRValue>(interrupt_enable);
    }
    mstatus |= previous_interrupt_enable;
}

auto sign_extend_return_pc(const ArchState& state, Address pc) -> Register {
    if (state.regs.xlen == 32) {
        return static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(pc)));
    }
    return pc;
}

}  // namespace

auto trap_cause_name(TrapCause cause) -> std::string {
    const bool is_interrupt = trap_is_interrupt(cause);
    const auto code = to_exception_code(cause);
    const uint64_t numeric_code = trap_exception_code(cause);
    if (is_interrupt) {
        switch (numeric_code) {
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
                return "Unknown interrupt " + std::to_string(numeric_code);
        }
    } else {
        switch (code) {
            case ExceptionCode::MisalignedFetch:
                return "Instruction address misaligned";
            case ExceptionCode::FaultFetch:
                return "Instruction access fault";
            case ExceptionCode::IllegalInstruction:
                return "Illegal instruction";
            case ExceptionCode::Breakpoint:
                return "Breakpoint";
            case ExceptionCode::MisalignedLoad:
                return "Load address misaligned";
            case ExceptionCode::FaultLoad:
                return "Load access fault";
            case ExceptionCode::MisalignedStore:
                return "Store/AMO address misaligned";
            case ExceptionCode::FaultStore:
                return "Store/AMO access fault";
            case ExceptionCode::UserEcall:
                return "Environment call from U-mode";
            case ExceptionCode::SupervisorEcall:
                return "Environment call from S-mode";
            case ExceptionCode::MachineEcall:
                return "Environment call from M-mode";
            case ExceptionCode::FetchPageFault:
                return "Instruction page fault";
            case ExceptionCode::LoadPageFault:
                return "Load page fault";
            case ExceptionCode::StorePageFault:
                return "Store/AMO page fault";
            default:
                return "Unknown exception " + std::to_string(numeric_code);
        }
    }
}

void TrapController::mret(ArchState& state) {
    CSRValue mstatus = state.mstatus;
    const PrivilegeLevel target_priv = mstatus_mpp(mstatus);

    restore_trap_interrupt_state(mstatus, MstatusBit::Mie, MstatusBit::Mpie);
    // xRET resets xPP to the least-privileged supported mode.
    const bool has_u = isa::misa_has_extension(state.misa, isa::IsaExtension::U);
    mstatus = mstatus_with_mpp(mstatus, mstatus_mpp_after_xret(has_u));

    if (target_priv < kPrivMachine) {
        mstatus &= ~MstatusBit::Mprv;
    }

    state.mstatus = mstatus;
    state.priv = target_priv;
    state.update_xlen();
    state.pc = sign_extend_return_pc(state, isa::epc_read_value(state.mepc, state.misa));
    state.reserved = 0;
}

void TrapController::sret(ArchState& state) {
    CSRValue mstatus = state.mstatus;
    const PrivilegeLevel target_priv = mstatus_spp(mstatus);

    restore_trap_interrupt_state(mstatus, MstatusBit::Sie, MstatusBit::Spie);
    mstatus = mstatus_with_spp(mstatus, mstatus_spp_after_sret());

    // Returning to a less privileged mode than M-mode always clears MPRV
    mstatus &= ~MstatusBit::Mprv;

    state.mstatus = mstatus;
    state.priv = target_priv;
    state.update_xlen();
    state.pc = sign_extend_return_pc(state, isa::epc_read_value(state.sepc, state.misa));
    state.reserved = 0;
}

void TrapController::raise_exception(CPU& cpu, TrapCause cause, CSRValue tval) {
    ArchState& state = cpu.state();
    const Address trap_pc = state.pc;
    const PrivilegeLevel source_mode = state.priv;

    if (cpu.machine_ != nullptr &&
        simrv::compiler::unlikely(cpu.machine_->trace().is_trap_log_enabled() ||
                                  cpu.machine_->trace().is_architecture_trace_enabled())) {
        cpu.machine_->trace().log_trap(cpu.clint_mmio.mtime.load(std::memory_order_relaxed),
                                       cpu.clint_mmio.mcycle, cause, trap_pc, state.priv, state,
                                       tval, cpu);
    }

    if (cpu.sbi.handle_ecall(cause)) {
        state.reserved = 0;
        cpu.pipeline_context.pending_exception = std::nullopt;
        cpu.pipeline_context.pending_tval = 0;
        return;
    }

    const bool delegated = trap_delegated_to_supervisor(state, cause);
    const PrivilegeLevel target_mode = delegated ? kPrivSupervisor : kPrivMachine;
    const MstatusBit interrupt_enable = delegated ? MstatusBit::Sie : MstatusBit::Mie;
    const MstatusBit previous_interrupt_enable = delegated ? MstatusBit::Spie : MstatusBit::Mpie;
    const CSRValue trap_vector = delegated ? state.stvec : state.mtvec;
    stack_trap_interrupt_state(state, interrupt_enable, previous_interrupt_enable);
    if (delegated) {
        state.scause = cause;
        state.sepc = trap_pc;
        state.stval = tval;
        state.mstatus = mstatus_with_spp(state.mstatus, state.priv);
    } else {
        state.mcause = cause;
        state.mepc = trap_pc;
        state.mtval = tval;
        state.mstatus = mstatus_with_mpp(state.mstatus, state.priv);
    }
    state.priv = target_mode;
    state.update_xlen();
    state.pc = calculate_trap_target(trap_vector, cause);
    if (state.regs.xlen == 32) {
        state.pc = static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(state.pc)));
    }
    if (cpu.machine_ != nullptr) {
        cpu.machine_->trace().log_architectural_trap_entry(cpu, cause, trap_pc, source_mode, tval);
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
    if (funct12 == isa::Funct12Priv::Mret) {
        return current_priv >= kPrivMachine;
    }
    if (funct12 == isa::Funct12Priv::Sret) {
        return misa_has_extension(misa, isa::IsaExtension::S) && current_priv >= kPrivSupervisor &&
               !(current_priv == kPrivSupervisor && (mstatus & MstatusBit::Tsr) != 0);
    }
    if (funct12 == isa::Funct12Priv::Uret) {
        return false;
    }
    if (funct12 == isa::Funct12Priv::Wfi) {
        return current_priv >= kPrivSupervisor &&
               !(current_priv == kPrivSupervisor && (mstatus & MstatusBit::Tw) != 0);
    }
    if (funct7 == isa::Funct7Priv::SfenceVma) {
        return misa_has_extension(misa, isa::IsaExtension::S) && current_priv >= kPrivSupervisor &&
               !(current_priv == kPrivSupervisor && (mstatus & MstatusBit::Tvm) != 0);
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
