/**
 * @file Cpu.cpp
 * @brief CPU core state/control implementation.
 */
#include "simrv/core/Cpu.hpp"

#include "simrv/core/Machine.hpp"
#include "simrv/core/Tracer.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

CPU::CPU() : plic_mmio(*this), clint_mmio(*this), csr_file(*this), sbi(*this) {
    state_.regs.fill(0);
    state_.regs.fill_fp(0);
}

void CPU::TLB_flush() { tlb.flush(); }

void CPU::TLB_flush(bool match_all_vaddr, Address vaddr, bool match_all_asid, Word asid) {
    tlb.flush_selective(match_all_vaddr, vaddr, match_all_asid, asid);
}

auto CPU::get_mstatus(CSRValue mask) const -> CSRValue { return csr_file.getMstatus(mask); }

void CPU::set_mstatus(CSRValue wdata) {
    const CSRValue old_mstatus = state_.mstatus;
    csr_file.setMstatus(wdata);
    if (state_.mstatus != old_mstatus) {
        TLB_flush();
    }
}

auto CPU::read_csr(CSRAddress addr) const -> std::expected<CSRValue, ExceptionCode> { return csr_file.read(addr); }

auto CPU::write_csr(CSRAddress addr, CSRValue wdata) -> std::expected<void, ExceptionCode> {
    const CSRValue old_mstatus = state_.mstatus;
    const CSRValue old_satp = state_.satp;

    auto result = csr_file.write(addr, wdata);
    if (!result) {
        return result;
    }

    if (state_.mstatus != old_mstatus || state_.satp != old_satp) {
        TLB_flush();
    }
    return {};
}

void CPU::mret() { TrapController::mret(state_, tlb); }

void CPU::sret() { TrapController::sret(state_, tlb); }

void CPU::plic_update_mip() { InterruptController::updateMip(plic_mmio, state_); }

void CPU::plic_set_irq(int irq_num, int state) {
    InterruptController::setIrq(plic_mmio, irq_num, state);
}

void CPU::raise_exception(TrapCause cause, CSRValue tval) {
    TrapController::raiseException(*this, cause, tval);
}

void CPU::evaluate_timer_interrupt() {
    if (clint_mmio.mtime >= clint_mmio.mtimecmp) {
        state_.mip |= enum_mask(MipBit::Mtip);
    } else {
        state_.mip &= ~enum_mask(MipBit::Mtip);
    }
}

void CPU::run_cycle(Machine& machine) {
    if (!pipeline_task.handle) {
        pipeline_task = run_pipeline_coroutine(machine);
    }

    auto err = pipeline_task.resume();
    if (err.has_value()) {
        raise_exception(static_cast<TrapCause>(err->code), err->tval);
    }

    clint_mmio.mcycle++;
    if (++clint_mmio.rtc_divider >= 10) {
        clint_mmio.mtime++;
        clint_mmio.rtc_divider = 0;
    }
}

auto CPU::run_pipeline_coroutine(Machine& machine) -> PipelineTask {
    while (true) {
        if (!fetch_stage(machine, state_.pc)) {
            co_yield StageError{.code = pipeline_context.pending_exception.value(),
                                .tval = pipeline_context.pending_tval};
            continue;
        }
        if (!decode_stage(machine)) {
            co_yield StageError{.code = pipeline_context.pending_exception.value(),
                                .tval = pipeline_context.pending_tval};
            continue;
        }
        if (!execute_stage(machine)) {
            co_yield StageError{.code = pipeline_context.pending_exception.value(),
                                .tval = pipeline_context.pending_tval};
            continue;
        }
        if (!memory_stage(machine)) {
            co_yield StageError{.code = pipeline_context.pending_exception.value(),
                                .tval = pipeline_context.pending_tval};
            continue;
        }
        if (!writeback_stage(machine)) {
            co_yield StageError{.code = pipeline_context.pending_exception.value(),
                                .tval = pipeline_context.pending_tval};
            continue;
        }
        if (!commit_stage(machine)) {
            co_yield StageError{.code = pipeline_context.pending_exception.value(),
                                .tval = pipeline_context.pending_tval};
            continue;
        }
        co_yield std::nullopt;
    }
}

auto CPU::fetch_stage(Machine& machine, Address pc) -> bool {
    state_.pc = pc;
    pipeline_context.pending_exception = std::nullopt;
    pipeline_context.pending_tval = 0;

    run_fetch_stage(machine);

    return !pipeline_context.pending_exception.has_value();
}

auto CPU::decode_stage(Machine& machine) -> bool {
    run_decode_stage(machine);
    return !pipeline_context.pending_exception.has_value();
}

auto CPU::execute_stage(Machine& machine) -> bool {
    run_execute_stage(machine);
    return !pipeline_context.pending_exception.has_value();
}

auto CPU::memory_stage(Machine& machine) -> bool {
    run_memory_stage(machine);
    return !pipeline_context.pending_exception.has_value();
}

auto CPU::writeback_stage(Machine& machine) -> bool {
    run_writeback_stage(machine);
    return !pipeline_context.pending_exception.has_value();
}

auto CPU::commit_stage(Machine& machine) -> bool {
    run_commit_stage(machine);
    return !pipeline_context.pending_exception.has_value();
}

}  // namespace simrv::core