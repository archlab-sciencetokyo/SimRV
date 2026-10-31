/**
 * @file TrapController.cpp
 * @brief RISC-V architectural trap vectoring, exception escalation, and privilege management.
 */
#include "simrv/core/TrapController.hpp"

#include <cstdint>
#include <string>

#include "simrv/CompilerHints.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/CsrTypes.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/Sbi.hpp"
#include "simrv/core/TelemetrySink.hpp"
#include "simrv/core/Tracer.hpp"
#include "simrv/isa/Common.hpp"
#include "simrv/isa/Priv.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::core {

namespace {

[[nodiscard]] constexpr auto calculate_trap_target(simrv::xlen::CsrValue tvec,
                                                   simrv::xlen::TrapCause cause) noexcept
    -> simrv::xlen::Address {
    const simrv::xlen::Address tvec_base = tvec & ~simrv::xlen::Address{3};
    const simrv::xlen::Word tvec_mode = tvec & 3;
    if (tvec_mode == 1 && trap_is_interrupt(cause)) {
        return tvec_base + 4 * trap_exception_code(cause);
    }
    return tvec_base;
}

auto trap_delegated_to_supervisor(const ArchState& state, simrv::xlen::TrapCause cause) -> bool {
    if (state.priv > kPrivSupervisor || !misa_has_extension(state.misa, isa::IsaExtension::S)) {
        return false;
    }
    const auto cause_code = static_cast<simrv::xlen::CsrValue>(trap_exception_code(cause));
    if (trap_is_interrupt(cause) && isa::misa_has_extension(state.misa, isa::IsaExtension::H) &&
        (cause_code == 2U || cause_code == 6U || cause_code == 10U)) {
        // VS-level interrupts are always delegated past M-mode to HS-mode.
        return true;
    }
    if (state.virt && isa::misa_has_extension(state.misa, isa::IsaExtension::H) &&
        !trap_is_interrupt(cause) &&
        (cause_code == 10U || (cause_code >= 20U && cause_code <= 23U))) {
        // VS ecall, guest-page-fault, and virtual-instruction traps are routed to HS-mode.
        return true;
    }
    if (cause_code >= xlen::kXLenBits) return false;
    const simrv::xlen::CsrValue delegation =
        trap_is_interrupt(cause) ? state.mideleg : state.medeleg;
    return ((delegation >> cause_code) & 1U) != 0;
}

auto trap_delegated_to_virtual_supervisor(const ArchState& state, simrv::xlen::TrapCause cause)
    -> bool {
    if (!state.virt || !isa::misa_has_extension(state.misa, isa::IsaExtension::H)) return false;
    const auto cause_code = static_cast<simrv::xlen::CsrValue>(trap_exception_code(cause));
    if (!trap_is_interrupt(cause) &&
        (cause_code == 10U || (cause_code >= 20U && cause_code <= 23U))) {
        return false;
    }
    const simrv::xlen::CsrValue delegation =
        trap_is_interrupt(cause) ? state.hideleg : state.hedeleg;
    return cause_code < xlen::kXLenBits && ((delegation >> cause_code) & 1U) != 0;
}

void stack_trap_interrupt_state(ArchState& state, MstatusBit interrupt_enable,
                                MstatusBit previous_interrupt_enable) {
    if ((state.mstatus & interrupt_enable) != 0) {
        state.mstatus |= previous_interrupt_enable;
    } else {
        state.mstatus &= ~static_cast<simrv::xlen::CsrValue>(previous_interrupt_enable);
    }
    state.mstatus &= ~static_cast<simrv::xlen::CsrValue>(interrupt_enable);
}

void restore_trap_interrupt_state(simrv::xlen::CsrValue& mstatus, MstatusBit interrupt_enable,
                                  MstatusBit previous_interrupt_enable) {
    if ((mstatus & previous_interrupt_enable) != 0) {
        mstatus |= interrupt_enable;
    } else {
        mstatus &= ~static_cast<simrv::xlen::CsrValue>(interrupt_enable);
    }
    mstatus |= previous_interrupt_enable;
}

auto sign_extend_return_pc(const ArchState& state, simrv::xlen::Address pc) noexcept
    -> simrv::xlen::Register {
    if (state.regs.xlen == 32) {
        return static_cast<simrv::xlen::Register>(static_cast<int64_t>(static_cast<int32_t>(pc)));
    }
    return pc;
}

[[nodiscard]] constexpr auto is_implemented_supervisor_csr(uint16_t address) noexcept -> bool {
    switch (address) {
        case csr_addr(Csr::Sstatus):
        case csr_addr(Csr::Sie):
        case csr_addr(Csr::Stvec):
        case csr_addr(Csr::Scounteren):
        case csr_addr(Csr::Sscratch):
        case csr_addr(Csr::Sepc):
        case csr_addr(Csr::Scause):
        case csr_addr(Csr::Stval):
        case csr_addr(Csr::Sip):
        case csr_addr(Csr::Satp):
        case csr_addr(Csr::Senvcfg):
        case csr_addr(Csr::Sstateen0):
        case csr_addr(Csr::Sstateen1):
        case csr_addr(Csr::Sstateen2):
        case csr_addr(Csr::Sstateen3):
        case csr_addr(Csr::Scountovf):
            return true;
        default:
            return false;
    }
}

auto trap_value_is_guest_virtual_address(simrv::xlen::TrapCause cause) -> bool {
    if (trap_is_interrupt(cause)) return false;
    switch (to_exception_code(cause)) {
        case isa::ExceptionCode::MisalignedFetch:
        case isa::ExceptionCode::FaultFetch:
        case isa::ExceptionCode::Breakpoint:
        case isa::ExceptionCode::MisalignedLoad:
        case isa::ExceptionCode::FaultLoad:
        case isa::ExceptionCode::MisalignedStore:
        case isa::ExceptionCode::FaultStore:
        case isa::ExceptionCode::FetchPageFault:
        case isa::ExceptionCode::LoadPageFault:
        case isa::ExceptionCode::StorePageFault:
        case isa::ExceptionCode::GuestInstructionPageFault:
        case isa::ExceptionCode::GuestLoadPageFault:
        case isa::ExceptionCode::GuestStorePageFault:
            return true;
        default:
            return false;
    }
}

}  // namespace

auto trap_cause_name(simrv::xlen::TrapCause cause) -> std::string {
    const bool is_interrupt = trap_is_interrupt(cause);
    const auto code = to_exception_code(cause);
    const uint64_t numeric_code = trap_exception_code(cause);
    if (is_interrupt) {
        const auto irq = to_interrupt_code(cause);
        switch (irq) {
            case InterruptCode::SupervisorSoftware:
                return "Supervisor software interrupt";
            case InterruptCode::MachineSoftware:
                return "Machine software interrupt";
            case InterruptCode::SupervisorTimer:
                return "Supervisor timer interrupt";
            case InterruptCode::MachineTimer:
                return "Machine timer interrupt";
            case InterruptCode::SupervisorExternal:
                return "Supervisor external interrupt";
            case InterruptCode::MachineExternal:
                return "Machine external interrupt";
            case InterruptCode::SupervisorGuestExternal:
                return "Supervisor guest external interrupt";
            case InterruptCode::CounterOverflow:
                return "Counter overflow interrupt";
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
            case ExceptionCode::VirtualInstruction:
                return "Virtual instruction";
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
            case ExceptionCode::GuestInstructionPageFault:
                return "Guest instruction page fault";
            case ExceptionCode::LoadPageFault:
                return "Load page fault";
            case ExceptionCode::GuestLoadPageFault:
                return "Guest load page fault";
            case ExceptionCode::StorePageFault:
                return "Store/AMO page fault";
            case ExceptionCode::GuestStorePageFault:
                return "Guest store/AMO page fault";
            case ExceptionCode::SoftwareCheck:
                return "Software check / pointer checking";
            case ExceptionCode::HardwareError:
                return "Hardware error";
            default:
                return "Unknown exception " + std::to_string(numeric_code);
        }
    }
}

auto TrapController::predict_mret(const ArchState& state) noexcept -> TrapReturnState {
    TrapReturnState ret{};
    simrv::xlen::CsrValue mstatus = state.mstatus;
    const simrv::xlen::PrivilegeLevel target_priv = mstatus_mpp(mstatus);
    const bool has_h = isa::misa_has_extension(state.misa, isa::IsaExtension::H);
    const bool return_to_guest =
        has_h && target_priv < kPrivMachine && (mstatus & MstatusBit::Mpv) != 0;

    restore_trap_interrupt_state(mstatus, MstatusBit::Mie, MstatusBit::Mpie);
    // xRET resets xPP to the least-privileged supported mode.
    const bool has_u = isa::misa_has_extension(state.misa, isa::IsaExtension::U);
    mstatus = mstatus_with_mpp(mstatus, mstatus_mpp_after_xret(has_u));
    mstatus &= ~MstatusBit::Mpv;

    if (target_priv < kPrivMachine) {
        mstatus &= ~MstatusBit::Mprv;
    }

    ret.target_pc = sign_extend_return_pc(state, isa::epc_read_value(state.mepc, state.misa));
    ret.target_priv = target_priv;
    ret.target_virtualization = return_to_guest;
    ret.next_mstatus = mstatus;
    ret.next_vsstatus = state.vsstatus;
    ret.next_hstatus = state.hstatus;
    return ret;
}

auto TrapController::predict_sret(const ArchState& state) noexcept -> TrapReturnState {
    TrapReturnState ret{};
    const bool return_to_guest = !state.virt && (state.hstatus & (simrv::xlen::CsrValue{1} << 7U));
    simrv::xlen::CsrValue status = state.virt ? state.vsstatus : state.mstatus;
    const simrv::xlen::PrivilegeLevel target_priv = mstatus_spp(status);

    restore_trap_interrupt_state(status, MstatusBit::Sie, MstatusBit::Spie);
    status = mstatus_with_spp(status, mstatus_spp_after_sret());
    status &= ~MstatusBit::Mprv;

    ret.next_mstatus = state.virt ? state.mstatus : status;
    ret.next_vsstatus = state.virt ? status : state.vsstatus;
    ret.next_hstatus = state.hstatus;
    if (return_to_guest) {
        ret.next_hstatus &= ~((simrv::xlen::CsrValue{1} << 7U) | (simrv::xlen::CsrValue{1} << 8U));
    }
    ret.target_virtualization = state.virt || return_to_guest;
    ret.target_priv = target_priv;
    const auto return_pc = state.virt ? state.vsepc : state.sepc;
    ret.target_pc = sign_extend_return_pc(state, isa::epc_read_value(return_pc, state.misa));
    return ret;
}

void TrapController::mret(ArchState& state) {
    const auto ret = predict_mret(state);
    state.mstatus = ret.next_mstatus;
    state.priv = ret.target_priv;
    state.virt = ret.target_virtualization;
    state.update_xlen();
    state.pc = sign_extend_return_pc(state, isa::epc_read_value(state.mepc, state.misa));
    state.lr_reservation_active = false;
}

void TrapController::sret(ArchState& state) {
    const auto ret = predict_sret(state);
    state.mstatus = ret.next_mstatus;
    state.vsstatus = ret.next_vsstatus;
    state.hstatus = ret.next_hstatus;
    state.virt = ret.target_virtualization;
    state.priv = ret.target_priv;
    state.update_xlen();
    state.pc = sign_extend_return_pc(state, ret.target_pc);
    state.lr_reservation_active = false;
}

void TrapController::raise_exception(CPU& cpu, simrv::xlen::TrapCause cause,
                                     simrv::xlen::CsrValue tval) {
    ArchState& state = cpu.state();
    const simrv::xlen::Address trap_pc = state.pc;
    const simrv::xlen::PrivilegeLevel source_mode = state.priv;
    const simrv::xlen::CsrValue reported_tval =
        !trap_is_interrupt(cause) && to_exception_code(cause) == isa::ExceptionCode::Breakpoint
            ? trap_pc
            : tval;

    if (cpu.machine_ != nullptr &&
        simrv::compiler::unlikely(cpu.machine_->trace().is_trap_log_enabled() ||
                                  cpu.machine_->trace().is_architecture_trace_enabled())) {
        cpu.machine_->trace().log_trap(cpu.clint_mmio.mtime.load(std::memory_order_relaxed),
                                       cpu.clint_mmio.mcycle, cause, trap_pc, state.priv, state,
                                       tval, cpu);
    }

    if (cpu.sbi.handle_ecall(cause)) {
        state.lr_reservation_active = false;
        cpu.pipeline_context.pending_exception = std::nullopt;
        cpu.pipeline_context.pending_tval = 0;
        return;
    }

    const bool guest_source = state.virt;
    const bool hypervisor_memory_access = cpu.active_context().hypervisor_memory_access_fault;
    const bool delegated = trap_delegated_to_supervisor(state, cause);
    const bool delegated_to_vs = delegated && trap_delegated_to_virtual_supervisor(state, cause);
    const bool delegated_to_hs =
        delegated && ((guest_source && !delegated_to_vs) || hypervisor_memory_access);
    const simrv::xlen::PrivilegeLevel target_mode = delegated ? kPrivSupervisor : kPrivMachine;
    const MstatusBit interrupt_enable =
        delegated && !delegated_to_vs ? MstatusBit::Sie : MstatusBit::Mie;
    const MstatusBit previous_interrupt_enable =
        delegated && !delegated_to_vs ? MstatusBit::Spie : MstatusBit::Mpie;
    const simrv::xlen::CsrValue trap_vector =
        delegated_to_vs ? state.vstvec : (delegated ? state.stvec : state.mtvec);
    if (delegated_to_vs) {
        auto& status = state.vsstatus;
        if ((status & MstatusBit::Sie) != 0) {
            status |= MstatusBit::Spie;
        } else {
            status &= ~static_cast<simrv::xlen::CsrValue>(MstatusBit::Spie);
        }
        status &= ~static_cast<simrv::xlen::CsrValue>(MstatusBit::Sie);
    } else {
        stack_trap_interrupt_state(state, interrupt_enable, previous_interrupt_enable);
    }
    if (delegated_to_vs) {
        const auto cause_code = trap_exception_code(cause);
        const auto vs_cause =
            trap_is_interrupt(cause) && (cause_code == 2U || cause_code == 6U || cause_code == 10U)
                ? (cause_code - 1U)
                : cause_code;
        state.vscause = (cause & kInterruptCauseBit) | vs_cause;
        state.vsepc = trap_pc;
        state.vstval = reported_tval;
        state.vsstatus = mstatus_with_spp(state.vsstatus, state.priv);
    } else if (delegated) {
        state.scause = cause;
        state.sepc = trap_pc;
        state.stval = reported_tval;
        state.mstatus = mstatus_with_spp(state.mstatus, state.priv);
        if (delegated_to_hs) {
            constexpr simrv::xlen::CsrValue kSpv = simrv::xlen::CsrValue{1} << 7U;
            constexpr simrv::xlen::CsrValue kSpvp = simrv::xlen::CsrValue{1} << 8U;
            if (hypervisor_memory_access) {
                // An HLV/HLVX/HSV fault is reported to HS with SPV=0. SPVP describes
                // the guest privilege selected for the explicit access and is preserved.
                state.hstatus &= ~kSpv;
            } else {
                state.hstatus = (state.hstatus & ~(kSpv | kSpvp)) | kSpv |
                                (source_mode == kPrivSupervisor ? kSpvp : 0);
            }
            if (hypervisor_memory_access || trap_value_is_guest_virtual_address(cause)) {
                state.hstatus |= simrv::xlen::CsrValue{1} << 6U;
            } else {
                state.hstatus &= ~(simrv::xlen::CsrValue{1} << 6U);
            }
            state.virt = false;
        }
        if (isa::misa_has_extension(state.misa, isa::IsaExtension::H)) {
            const auto exception = trap_exception_code(cause);
            const bool guest_page_fault =
                !trap_is_interrupt(cause) &&
                (exception == std::to_underlying(isa::ExceptionCode::GuestInstructionPageFault) ||
                 exception == std::to_underlying(isa::ExceptionCode::GuestLoadPageFault) ||
                 exception == std::to_underlying(isa::ExceptionCode::GuestStorePageFault));
            state.htval = guest_page_fault ? cpu.active_context().pending_htval : 0;
            state.htinst = guest_page_fault ? cpu.active_context().pending_htinst : 0;
        }
    } else {
        state.mcause = cause;
        state.mepc = trap_pc;
        state.mtval = reported_tval;
        if (isa::misa_has_extension(state.misa, isa::IsaExtension::H)) {
            const auto exception = trap_exception_code(cause);
            const bool guest_page_fault =
                !trap_is_interrupt(cause) &&
                (exception == std::to_underlying(isa::ExceptionCode::GuestInstructionPageFault) ||
                 exception == std::to_underlying(isa::ExceptionCode::GuestLoadPageFault) ||
                 exception == std::to_underlying(isa::ExceptionCode::GuestStorePageFault));
            state.mtval2 = guest_page_fault ? (cpu.active_context().pending_htval >> 2U) : 0;
            state.mtinst = 0;
        }
        state.mstatus = mstatus_with_mpp(state.mstatus, source_mode);
        state.mstatus &= ~MstatusBit::Mpv;
        if (guest_source && isa::misa_has_extension(state.misa, isa::IsaExtension::H)) {
            state.mstatus |= MstatusBit::Mpv;
        }
        if ((guest_source || hypervisor_memory_access) &&
            isa::misa_has_extension(state.misa, isa::IsaExtension::H) &&
            (hypervisor_memory_access || trap_value_is_guest_virtual_address(cause))) {
            state.mstatus |= MstatusBit::Gva;
        } else {
            state.mstatus &= ~MstatusBit::Gva;
        }
        state.virt = false;
    }
    state.priv = target_mode;
    state.update_xlen();
    state.pc = calculate_trap_target(trap_vector, cause);
    if (state.regs.xlen == 32) {
        state.pc = static_cast<simrv::xlen::Register>(
            static_cast<int64_t>(static_cast<int32_t>(state.pc)));
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
    state.lr_reservation_active = false;
    cpu.pipeline_context.pending_exception = std::nullopt;
    cpu.pipeline_context.pending_tval = 0;
    cpu.pipeline_context.pending_htval = 0;
    cpu.pipeline_context.pending_htinst = 0;
}

auto TrapController::check_privileged_instruction(const ArchState& state,
                                                  simrv::isa::OperationId op_id,
                                                  simrv::xlen::Instruction ir) noexcept
    -> std::optional<simrv::isa::ExceptionCode> {
    using simrv::isa::ExceptionCode;
    using simrv::isa::Funct12Priv;
    using simrv::isa::Funct7Priv;
    using simrv::isa::Opcode;

    if (isa::is_hypervisor_memory_op(op_id)) {
        constexpr simrv::xlen::CsrValue kHstatusHu = simrv::xlen::CsrValue{1} << 9U;
        if (!isa::misa_has_extension(state.misa, isa::IsaExtension::H)) {
            return ExceptionCode::IllegalInstruction;
        }
        if (state.virt) {
            return ExceptionCode::VirtualInstruction;
        }
        if (state.priv == kPrivUser && (state.hstatus & kHstatusHu) == 0) {
            return ExceptionCode::IllegalInstruction;
        }
        if (state.priv != kPrivMachine && state.priv != kPrivSupervisor &&
            state.priv != kPrivUser) {
            return ExceptionCode::IllegalInstruction;
        }
        return std::nullopt;
    }

    if (op_id == isa::OperationId::WRS_NTO && state.lr_reservation_active &&
        state.priv < kPrivMachine) {
        const bool timeout_wait = (state.mstatus & MstatusBit::Tw) != 0;
        const bool virtual_timeout_trap =
            !timeout_wait && state.virt && (state.hstatus & (simrv::xlen::CsrValue{1} << 21U)) != 0;
        if (timeout_wait || virtual_timeout_trap) {
            return virtual_timeout_trap ? ExceptionCode::VirtualInstruction
                                        : ExceptionCode::IllegalInstruction;
        }
        return std::nullopt;
    }

    const auto opcode = isa::opcode_of(ir);
    const auto funct3 = isa::funct3_of(ir);
    if (opcode == Opcode::System && funct3 == isa::Funct3::Priv &&
        op_id != isa::OperationId::ECALL && op_id != isa::OperationId::EBREAK &&
        op_id != isa::OperationId::MOP && op_id != isa::OperationId::WRS_NTO &&
        op_id != isa::OperationId::WRS_STO) {
        const simrv::xlen::Instruction funct12 = isa::funct12_of(ir);
        const simrv::xlen::Word funct7 = isa::funct7_of(ir);
        if (!can_execute_privileged_instruction(state.priv, state.misa, state.mstatus, funct12,
                                                funct7, state.hstatus, state.virt)) {
            const bool virtual_instruction =
                state.virt &&
                ((funct12 == Funct12Priv::Sret &&
                  (state.priv == kPrivUser ||
                   (state.hstatus & (simrv::xlen::CsrValue{1} << 22U)) != 0)) ||
                 (funct12 == Funct12Priv::Wfi && (state.mstatus & MstatusBit::Tw) == 0 &&
                  (state.priv == kPrivUser ||
                   (state.hstatus & (simrv::xlen::CsrValue{1} << 21U)) != 0)) ||
                 funct7 == Funct7Priv::HfenceVvma || funct7 == Funct7Priv::HinvalVvma ||
                 funct7 == Funct7Priv::HfenceGvma || funct7 == Funct7Priv::HinvalGvma ||
                 ((funct12 == Funct12Priv::SfenceWInval || funct12 == Funct12Priv::SfenceInvalIr) &&
                  state.priv == kPrivUser) ||
                 ((funct7 == Funct7Priv::SfenceVma || funct7 == Funct7Priv::SinvalVma) &&
                  (state.priv == kPrivUser ||
                   (state.hstatus & (simrv::xlen::CsrValue{1} << 20U)) != 0)));
            return virtual_instruction ? ExceptionCode::VirtualInstruction
                                       : ExceptionCode::IllegalInstruction;
        }
    }

    return std::nullopt;
}

auto TrapController::check_csr_access(const ArchState& state, simrv::xlen::CsrNumber csr_number,
                                      bool is_write) noexcept
    -> std::optional<simrv::isa::ExceptionCode> {
    using simrv::isa::ExceptionCode;
    const uint16_t address = csr_number.value();
    const unsigned active_xlen = state.current_xlen();

    if (!can_access_csr(state.priv, state.misa, csr_number, is_write, state.virt)) {
        const bool virtual_supervisor_csr =
            address == csr_addr(Csr::Vsstatus) || address == csr_addr(Csr::Vsie) ||
            address == csr_addr(Csr::Vstvec) || address == csr_addr(Csr::Vsscratch) ||
            address == csr_addr(Csr::Vsepc) || address == csr_addr(Csr::Vscause) ||
            address == csr_addr(Csr::Vstval) || address == csr_addr(Csr::Vsip) ||
            address == csr_addr(Csr::Vsatp) ||
            ((address == csr_addr(Csr::Vstimecmp) ||
              (address == csr_addr(Csr::Vstimecmph) && active_xlen == 32)) &&
             state.priv == kPrivSupervisor);
        const bool virtual_hypervisor_csr =
            address == csr_addr(Csr::Hstatus) || address == csr_addr(Csr::Hedeleg) ||
            address == csr_addr(Csr::Hideleg) || address == csr_addr(Csr::Hie) ||
            address == csr_addr(Csr::Hcounteren) || address == csr_addr(Csr::Htimedelta) ||
            address == csr_addr(Csr::Hgeie) || (address == csr_addr(Csr::Hgeip) && !is_write) ||
            address == csr_addr(Csr::Henvcfg) || address == csr_addr(Csr::Htval) ||
            address == csr_addr(Csr::Hip) || address == csr_addr(Csr::Hvip) ||
            address == csr_addr(Csr::Htinst) || address == csr_addr(Csr::Hgatp) ||
            (address >= 0x60CU && address <= 0x60FU);
        const bool virtualization_fault =
            state.virt &&
            ((state.priv == kPrivUser &&
              isa::misa_has_extension(state.misa, isa::IsaExtension::S) &&
              is_implemented_supervisor_csr(address)) ||
             (address == csr_addr(Csr::Satp) &&
              (state.priv == kPrivUser ||
               (state.hstatus & (simrv::xlen::CsrValue{1} << 20U)) != 0)) ||
             (state.priv < kPrivMachine && (virtual_hypervisor_csr || virtual_supervisor_csr)));
        return virtualization_fault ? ExceptionCode::VirtualInstruction
                                    : ExceptionCode::IllegalInstruction;
    }

    if (address == csr_addr(Csr::Satp)) {
        if (state.virt && (state.hstatus & (simrv::xlen::CsrValue{1} << 20U)) != 0) {
            return ExceptionCode::VirtualInstruction;
        }
        if (!state.virt && state.priv == kPrivSupervisor &&
            (state.mstatus & MstatusBit::Tvm) != 0) {
            return ExceptionCode::IllegalInstruction;
        }
    }

    if (state.priv < kPrivMachine) {
        if ((address >= 0xC00U && address <= 0xC1FU) || (address >= 0xC80U && address <= 0xC9FU)) {
            const simrv::xlen::Word counter_bit = 1U << (address & 0x1FU);
            if ((state.mcounteren & counter_bit) == 0) {
                return ExceptionCode::IllegalInstruction;
            }
            if (state.virt && (state.hcounteren & counter_bit) == 0) {
                return ExceptionCode::VirtualInstruction;
            }
            if (state.priv == kPrivUser && (state.scounteren & counter_bit) == 0) {
                return state.virt ? ExceptionCode::VirtualInstruction
                                  : ExceptionCode::IllegalInstruction;
            }
        }
    }

    if (address == csr_addr(Csr::Fflags) || address == csr_addr(Csr::Frm) ||
        address == csr_addr(Csr::Fcsr)) {
        if (!isa::misa_has_extension(state.misa, isa::IsaExtension::F) ||
            !state.extension_state_enabled(MstatusBit::Fs)) {
            return ExceptionCode::IllegalInstruction;
        }
    }

    if (address == csr_addr(Csr::Vstart) || address == csr_addr(Csr::Vxsat) ||
        address == csr_addr(Csr::Vxrm) || address == csr_addr(Csr::Vcsr) ||
        address == csr_addr(Csr::Vl) || address == csr_addr(Csr::Vtype) ||
        address == csr_addr(Csr::Vlenb)) {
        if (!isa::misa_has_extension(state.misa, isa::IsaExtension::V) ||
            !state.extension_state_enabled(MstatusBit::Vs)) {
            return ExceptionCode::IllegalInstruction;
        }
    }

    constexpr uint64_t kStateenSuperEnable = uint64_t{1} << 63U;
    constexpr uint64_t kStateenEnvCfgEnable = uint64_t{1} << 62U;
    if ((address >= 0x31CU && address <= 0x31FU) || (address >= 0x71CU && address <= 0x71FU)) {
        if (active_xlen != 32) {
            return ExceptionCode::IllegalInstruction;
        }
    }
    if (address >= 0x10CU && address <= 0x10FU) {
        const size_t bank = address - 0x10CU;
        if (state.virt && isa::misa_has_extension(state.misa, isa::IsaExtension::H) &&
            (state.hstateen[bank] & kStateenSuperEnable) == 0) {
            return ExceptionCode::VirtualInstruction;
        }
        if (state.priv != kPrivMachine && (state.mstateen[bank] & kStateenSuperEnable) == 0) {
            return ExceptionCode::IllegalInstruction;
        }
    }
    if (address >= 0x60CU && address <= 0x60FU) {
        const size_t bank = address - 0x60CU;
        if (state.priv != kPrivMachine && (state.mstateen[bank] & kStateenSuperEnable) == 0 &&
            isa::misa_has_extension(state.misa, isa::IsaExtension::H)) {
            return ExceptionCode::IllegalInstruction;
        }
    }
    if (address == csr_addr(Csr::Senvcfg)) {
        if (state.virt && isa::misa_has_extension(state.misa, isa::IsaExtension::H) &&
            (state.hstateen[0] & kStateenEnvCfgEnable) == 0) {
            return ExceptionCode::VirtualInstruction;
        }
        if (state.priv != kPrivMachine && (state.mstateen[0] & kStateenEnvCfgEnable) == 0) {
            return ExceptionCode::IllegalInstruction;
        }
    }
    if (address == csr_addr(Csr::Henvcfg)) {
        if (state.priv != kPrivMachine && (state.mstateen[0] & kStateenEnvCfgEnable) == 0) {
            return ExceptionCode::IllegalInstruction;
        }
    }
    if (address == csr_addr(Csr::Stimecmp)) {
        if (state.priv < kPrivMachine) {
            if ((state.mcounteren & (simrv::xlen::CsrValue{1} << 1U)) == 0) {
                return ExceptionCode::IllegalInstruction;
            }
            bool stce = false;
            if constexpr (simrv::xlen::kIsXLen64) {
                stce = (state.menvcfg & (1ULL << 63U)) != 0;
            } else {
                stce = (state.menvcfgh & (1UL << 31U)) != 0;
            }
            if (!stce) {
                return ExceptionCode::IllegalInstruction;
            }
            if (state.virt) {
                if ((state.henvcfg & static_cast<simrv::xlen::CsrValue>(uint64_t{1} << 63U)) == 0 ||
                    (state.hcounteren & (simrv::xlen::CsrValue{1} << 1U)) == 0) {
                    return ExceptionCode::VirtualInstruction;
                }
            }
        }
    }
    if (address == csr_addr(Csr::Stimecmph)) {
        if (active_xlen != 32) {
            return ExceptionCode::IllegalInstruction;
        }
        if (state.priv < kPrivMachine) {
            const bool stce = (state.menvcfgh & (1UL << 31U)) != 0;
            if (!stce) {
                return ExceptionCode::IllegalInstruction;
            }
        }
    }

    return std::nullopt;
}

auto TrapController::can_execute_privileged_instruction(
    simrv::xlen::PrivilegeLevel current_priv, simrv::xlen::CsrValue misa,
    simrv::xlen::CsrValue mstatus, simrv::xlen::Instruction funct12, simrv::xlen::Word funct7,
    simrv::xlen::CsrValue hstatus, bool virtualization_mode) -> bool {
    if (funct12 == isa::Funct12Priv::Mret) {
        return current_priv >= kPrivMachine;
    }
    if (funct12 == isa::Funct12Priv::Sret) {
        return misa_has_extension(misa, isa::IsaExtension::S) && current_priv >= kPrivSupervisor &&
               !(virtualization_mode && (hstatus & (simrv::xlen::CsrValue{1} << 22U)) != 0) &&
               !(current_priv == kPrivSupervisor && !virtualization_mode &&
                 (mstatus & MstatusBit::Tsr) != 0);
    }
    if (funct12 == isa::Funct12Priv::Uret) {
        return false;
    }
    if (funct12 == isa::Funct12Priv::Wfi) {
        return current_priv >= kPrivSupervisor &&
               !(virtualization_mode && (hstatus & (simrv::xlen::CsrValue{1} << 21U)) != 0) &&
               !(current_priv < kPrivMachine && (mstatus & MstatusBit::Tw) != 0);
    }
    if (funct7 == isa::Funct7Priv::HfenceVvma || funct7 == isa::Funct7Priv::HinvalVvma ||
        funct7 == isa::Funct7Priv::HfenceGvma || funct7 == isa::Funct7Priv::HinvalGvma) {
        return misa_has_extension(misa, isa::IsaExtension::H) &&
               (current_priv == kPrivMachine ||
                (current_priv == kPrivSupervisor && !virtualization_mode));
    }
    if (funct12 == isa::Funct12Priv::SfenceWInval || funct12 == isa::Funct12Priv::SfenceInvalIr) {
        return misa_has_extension(misa, isa::IsaExtension::S) && current_priv >= kPrivSupervisor;
    }
    if (funct7 == isa::Funct7Priv::SfenceVma || funct7 == isa::Funct7Priv::SinvalVma) {
        return misa_has_extension(misa, isa::IsaExtension::S) && current_priv >= kPrivSupervisor &&
               !(virtualization_mode && (hstatus & (simrv::xlen::CsrValue{1} << 20U)) != 0) &&
               !(current_priv == kPrivSupervisor && !virtualization_mode &&
                 (mstatus & MstatusBit::Tvm) != 0);
    }
    return true;
}

auto TrapController::can_access_csr(simrv::xlen::PrivilegeLevel current_priv,
                                    simrv::xlen::CsrValue misa, simrv::xlen::CsrNumber csr_addr,
                                    bool is_write, bool virtualization_mode) -> bool {
    // Debug CSRs require architectural Debug Mode, which the guest execution
    // engine does not implement. M-mode also cannot override CSR nonexistence.
    const auto address = csr_addr.value();
    if ((address == csr_num(Csr::Mtinst).value() || address == csr_num(Csr::Mtval2).value()) &&
        !isa::misa_has_extension(misa, isa::IsaExtension::H)) {
        return false;
    }
    const bool h_csr =
        address == csr_num(Csr::Hstatus).value() || address == csr_num(Csr::Hedeleg).value() ||
        address == csr_num(Csr::Hideleg).value() || address == csr_num(Csr::Hie).value() ||
        address == csr_num(Csr::Hcounteren).value() ||
        address == csr_num(Csr::Htimedelta).value() || address == csr_num(Csr::Hgeie).value() ||
        address == csr_num(Csr::Hgeip).value() || address == csr_num(Csr::Henvcfg).value() ||
        address == csr_num(Csr::Htval).value() || address == csr_num(Csr::Hip).value() ||
        address == csr_num(Csr::Hvip).value() || address == csr_num(Csr::Htinst).value() ||
        address == csr_num(Csr::Hgatp).value() || (address >= 0x60CU && address <= 0x60FU);
    const bool vs_csr = address >= 0x200U && address <= 0x2FFU;
    if (h_csr || vs_csr) {
        if (!isa::misa_has_extension(misa, isa::IsaExtension::H)) return false;
        if (virtualization_mode && current_priv < kPrivMachine) return false;
        if (current_priv < kPrivSupervisor) return false;
        return !is_write || !csr_addr.is_read_only();
    }
    return csr_access_permitted(current_priv, isa::misa_has_extension(misa, isa::IsaExtension::S),
                                isa::misa_has_extension(misa, isa::IsaExtension::U), csr_addr,
                                is_write);
}

}  // namespace simrv::core
