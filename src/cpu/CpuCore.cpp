/**
 * @file CpuCore.cpp
 * @brief CPU core state/control implementation.
 */
#include "Cpu.hpp"
#include "XLen.hpp"

CPU::CPU()
    :  tlb_unit(*this),
      interrupt_controller(*this),
      plic_mmio(*this),
      clint_mmio(*this),
      trap_controller(*this),
      csr_file(*this) {
    reg.fill(0);
    freg.fill(0);
    
}

void CPU::TLB_flush() { tlb_unit.flush(); }

auto CPU::get_mstatus(CSRValue mask) const -> CSRValue { return csr_file.getMstatus(mask); }

void CPU::set_mstatus(CSRValue wdata) { csr_file.setMstatus(wdata); }

auto CPU::read_csr(CSRAddress addr) const -> CSRValue { return csr_file.read(addr); }

void CPU::write_csr(CSRAddress addr, CSRValue wdata) { csr_file.write(addr, wdata); }

void CPU::mret() { trap_controller.mret(); }

void CPU::sret() { trap_controller.sret(); }

void CPU::plic_update_mip() { interrupt_controller.updateMip(); }

void CPU::plic_set_irq(int irq_num, int state) { interrupt_controller.setIrq(irq_num, state); }

void CPU::raise_exception(TrapCause cause, CSRValue tval) {
    trap_controller.raiseException(cause, tval);
}
