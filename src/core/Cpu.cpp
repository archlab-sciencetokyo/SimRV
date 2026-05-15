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

CPU::CPU()
    : TLB_inst_r(tlb.inst_r),
      TLB_data_r(tlb.data_r),
      TLB_data_w(tlb.data_w),
      plic_mmio(*this),
      clint_mmio(*this),
      csr_file(*this) {
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

auto CPU::read_csr(CSRAddress addr) const -> CSRValue { return csr_file.read(addr); }

void CPU::write_csr(CSRAddress addr, CSRValue wdata) {
    const CSRValue old_mstatus = state_.mstatus;
    const CSRValue old_satp = state_.satp;

    csr_file.write(addr, wdata);

    if (state_.mstatus != old_mstatus || state_.satp != old_satp) {
        TLB_flush();
    }
}

void CPU::mret() { TrapController::mret(state_, tlb); }

void CPU::sret() { TrapController::sret(state_, tlb); }

void CPU::plic_update_mip() { InterruptController::updateMip(plic_mmio, state_); }

void CPU::plic_set_irq(int irq_num, int state) {
    InterruptController::setIrq(plic_mmio, state_, irq_num, state);
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
    run_fetch_stage(machine);
    run_decode_stage(machine);
    run_execute_stage(machine);
    run_memory_stage(machine);
    run_writeback_stage(machine);
    run_commit_stage(machine);

    clint_mmio.mtime++;
}

}  // namespace simrv::core