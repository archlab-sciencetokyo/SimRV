/**
 * @file StateControl.cpp
 * @brief SimRV implementation unit.
 */
#include "StateControl.hpp"

#include "State.hpp"

void TlbUnit::flush() {
    for (Word i = 0; i < simrv::memory::kTlbSize; i++) {
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

bool PlicMmio::read(Machine& machine, Address p_addr, Word& rdata) {
    (void)machine;
    rdata = mmio_read(offset(p_addr));
    return true;
}

bool PlicMmio::write(Machine& machine, Address p_addr, Word wdata) {
    (void)machine;
    mmio_write(offset(p_addr), wdata);
    return true;
}

Word PlicMmio::mmio_read(Address offset) {
    if (offset == simrv::mmio::kPlicHartBase + 4) {
        CSRValue mask = cpu_.plic_pending_irq & ~cpu_.plic_served_irq;
        if (mask != 0) {
            cpu_.plic_served_irq |= mask;
            cpu_.plic_update_mip();
            return mask;
        }
    }
    return 0;
}

void PlicMmio::mmio_write(Address offset, Word wdata) {
    if (offset == simrv::mmio::kPlicHartBase + 4) {
        cpu_.plic_served_irq &= ~(1u << (wdata - 1));
        cpu_.plic_update_mip();
    }
}

bool ClintMmio::read(Machine& machine, Address p_addr, Word& rdata) {
    (void)machine;
    rdata = mmio_read(offset(p_addr));
    return true;
}

bool ClintMmio::write(Machine& machine, Address p_addr, Word wdata) {
    (void)machine;
    mmio_write(offset(p_addr), wdata);
    return true;
}

Word ClintMmio::mmio_read(Address offset) {
    if (offset == 0xbff8) return static_cast<Word>(cpu_.mtime);
    if (offset == 0xbffc) return static_cast<Word>(cpu_.mtime >> 32);
    if (offset == 0x4000) return static_cast<Word>(cpu_.mtimecmp);
    if (offset == 0x4004) return static_cast<Word>(cpu_.mtimecmp >> 32);
    return 0;
}

void ClintMmio::mmio_write(Address offset, Word wdata) {
    if (offset == 0x4000) {
        cpu_.mtimecmp = (cpu_.mtimecmp & ~0xffffffffu) | wdata;
        cpu_.mip &= ~enum_mask(MipBit::Mtip);
    }
    if (offset == 0x4004) {
        cpu_.mtimecmp = (cpu_.mtimecmp & 0xffffffffu) | ((Counter)wdata << 32);
        cpu_.mip &= ~enum_mask(MipBit::Mtip);
    }
}

void TrapController::mret() {
    MstatusView mstatus(cpu_.mstatus);
    const CSRValue mpp = mstatus.bits.mpp;
    const CSRValue mpie = mstatus.bits.mpie;

    mstatus.rawValue = (mstatus.rawValue & ~(static_cast<CSRValue>(1) << mpp)) | (mpie << mpp);
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

    mstatus.rawValue = (mstatus.rawValue & ~(static_cast<CSRValue>(1) << spp)) | (spie << spp);
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
        cpu_.mstatus = (cpu_.mstatus & ~enum_mask(MstatusBit::Spie)) |
                       (((cpu_.mstatus >> cpu_.priv) & 1) << 5);
        cpu_.mstatus = (cpu_.mstatus & ~enum_mask(MstatusBit::Spp)) | (cpu_.priv << 8);
        cpu_.mstatus &= ~enum_mask(MstatusBit::Sie);
        cpu_.priv = kPrivSupervisor;
        cpu_.pc = cpu_.stvec;
    } else {
        cpu_.mcause = cause;
        cpu_.mepc = cpu_.pc;
        cpu_.mtval = tval;
        cpu_.mstatus = (cpu_.mstatus & ~enum_mask(MstatusBit::Mpie)) |
                       (((cpu_.mstatus >> cpu_.priv) & 1) << 7);
        cpu_.mstatus = (cpu_.mstatus & ~enum_mask(MstatusBit::Mpp)) | (cpu_.priv << 11);
        cpu_.mstatus &= ~enum_mask(MstatusBit::Mie);
        cpu_.priv = kPrivMachine;
        cpu_.pc = cpu_.mtvec;
    }
    cpu_.TLB_flush();
}
