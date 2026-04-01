/**
 * @file State.cpp
 * @brief SimRV implementation unit.
 */
#include "State.hpp"

CPU::CPU()
    : tlb_unit(*this), interrupt_controller(*this), trap_controller(*this), csr_file(*this) {}

void CPU::TLB_flush() { tlb_unit.flush(); }

CSRValue CPU::get_mstatus(CSRValue mask) { return csr_file.getMstatus(mask); }

void CPU::set_mstatus(CSRValue wdata) { csr_file.setMstatus(wdata); }

CSRValue CPU::read_csr(CSRAddress addr) { return csr_file.read(addr); }

void CPU::write_csr(CSRAddress addr, CSRValue wdata) { csr_file.write(addr, wdata); }

void CPU::mret() { trap_controller.mret(); }

void CPU::sret() { trap_controller.sret(); }

void CPU::plic_update_mip() { interrupt_controller.updateMip(); }

void CPU::plic_set_irq(int irq_num, int state) { interrupt_controller.setIrq(irq_num, state); }

void CPU::raise_exception(TrapCause cause, CSRValue tval) {
    trap_controller.raiseException(cause, tval);
}
