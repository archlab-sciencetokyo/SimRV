/**
 * @file StateControl.cpp
 * @brief SimRV implementation unit.
 */
#include "StateControl.hpp"

#include "State.hpp"

void TlbUnit::flush() {
    for (Word i = 0; i < TLB_SIZE; i++) {
        cpu_.TLB_inst_r[i].v_addr = cpu_.TLB_inst_r[i].p_addr = static_cast<Word>(-1);
        cpu_.TLB_data_r[i].v_addr = cpu_.TLB_data_r[i].p_addr = static_cast<Word>(-1);
        cpu_.TLB_data_w[i].v_addr = cpu_.TLB_data_w[i].p_addr = static_cast<Word>(-1);
    }
}

void InterruptController::updateMip() {
    CSRValue mask = cpu_.plic_pending_irq & ~cpu_.plic_served_irq;
    const CSRValue ext_irq = enum_mask(MipBit::Meip) | enum_mask(MipBit::Seip);
    if (mask) {
        cpu_.mip |= ext_irq;
    } else {
        cpu_.mip &= ~ext_irq;
    }
}

void InterruptController::setIrq(int irq_num, int state) {
    CSRValue mask = static_cast<CSRValue>(1u) << (irq_num - 1);
    if (state) {
        cpu_.plic_pending_irq |= mask;
    } else {
        cpu_.plic_pending_irq &= ~mask;
    }
    updateMip();
}

void TrapController::mret() {
    MstatusView mstatus(cpu_.mstatus);
    const CSRValue mpp = mstatus.bits.mpp;
    const CSRValue mpie = mstatus.bits.mpie;

    mstatus.raw32 = (mstatus.raw32 & ~(static_cast<CSRValue>(1) << mpp)) | (mpie << mpp);
    mstatus.bits.mpie = 1;
    mstatus.bits.mpp = 0;
    cpu_.mstatus = mstatus.raw();
    cpu_.priv = mpp;
    cpu_.pc = cpu_.mepc;
    cpu_.TLB_flush();
}

void TrapController::sret() {
    MstatusView mstatus(cpu_.mstatus);
    const CSRValue spp = mstatus.bits.spp;
    const CSRValue spie = mstatus.bits.spie;

    mstatus.raw32 = (mstatus.raw32 & ~(static_cast<CSRValue>(1) << spp)) | (spie << spp);
    mstatus.bits.spie = 1;
    mstatus.bits.spp = 0;
    cpu_.mstatus = mstatus.raw();
    cpu_.priv = spp;
    cpu_.pc = cpu_.sepc;
    cpu_.TLB_flush();
}

void TrapController::raiseException(TrapCause cause, CSRValue tval) {
    CSRValue deleg;
    if (cpu_.priv <= kPrivSupervisor) {
        if (cause & kInterruptCauseBit) {
            deleg = (cpu_.mideleg >> (cause & 0x1F)) & 1;
        } else {
            deleg = (cpu_.medeleg >> (cause & 0x1F)) & 1;
        }
    } else {
        deleg = 0;
    }

    if (deleg) {
        cpu_.scause = cause;
        cpu_.sepc = cpu_.pc;
        cpu_.stval = tval;
        cpu_.mstatus = (cpu_.mstatus & ~kMstatusSpie) |
                       (((cpu_.mstatus >> cpu_.priv) & 1) << kMstatusSpieShift);
        cpu_.mstatus = (cpu_.mstatus & ~kMstatusSpp) | (cpu_.priv << kMstatusSppShift);
        cpu_.mstatus &= ~kMstatusSie;
        cpu_.priv = kPrivSupervisor;
        cpu_.pc = cpu_.stvec;
    } else {
        cpu_.mcause = cause;
        cpu_.mepc = cpu_.pc;
        cpu_.mtval = tval;
        cpu_.mstatus = (cpu_.mstatus & ~kMstatusMpie) |
                       (((cpu_.mstatus >> cpu_.priv) & 1) << kMstatusMpieShift);
        cpu_.mstatus = (cpu_.mstatus & ~kMstatusMpp) | (cpu_.priv << kMstatusMppShift);
        cpu_.mstatus &= ~kMstatusMie;
        cpu_.priv = kPrivMachine;
        cpu_.pc = cpu_.mtvec;
    }
    cpu_.TLB_flush();
}
