/**
 * @file CsrFile.cpp
 * @brief SimRV implementation unit.
 */
#include "CsrFile.hpp"

#include "Cpu.hpp"

auto CsrFile::getMstatus(CSRValue mask) const -> CSRValue {
    CSRValue val = (cpu_.mstatus | kMstatusFsDirty) & mask;
    return (((val >> 13) == 3) | ((val >> 15) == 3)) ? (val | kMstatusSd) : val;
}

void CsrFile::setMstatus(CSRValue wdata) {
    CSRValue mod = cpu_.mstatus ^ wdata;
    const CSRValue tlb_sensitive =
        enum_mask(MstatusBit::Mprv) | enum_mask(MstatusBit::Sum) | enum_mask(MstatusBit::Mxr);
    if ((mod & tlb_sensitive) != 0 ||
        ((cpu_.mstatus & enum_mask(MstatusBit::Mprv)) && (mod & enum_mask(MstatusBit::Mpp)) != 0)) {
        cpu_.TLB_flush();
    }
    CSRValue mask = kMstatusMask & ~enum_mask(MstatusBit::Fs);
    cpu_.mstatus = (cpu_.mstatus & ~mask) | (wdata & mask);
}

auto CsrFile::read(CSRAddress addr) const -> CSRValue {
    CSRValue rcsr = 0;
    switch (addr) {
        case csr_addr(Csr::Pmpcfg0):
        case csr_addr(Csr::Pmpaddr0):
            rcsr = 0;
            break;

        case csr_addr(Csr::Fflags):
            rcsr = cpu_.fcsr & kFflagsMask;
            break;
        case csr_addr(Csr::Frm):
            rcsr = (cpu_.fcsr >> 5) & 0x7;
            break;
        case csr_addr(Csr::Fcsr):
            rcsr = cpu_.fcsr & 0xff;
            break;

        case csr_addr(Csr::Sie):
            rcsr = cpu_.mie & cpu_.mideleg;
            break;
        case csr_addr(Csr::Stvec):
            rcsr = cpu_.stvec;
            break;
        case csr_addr(Csr::Scounteren):
            rcsr = cpu_.scounteren;
            break;
        case csr_addr(Csr::Sscratch):
            rcsr = cpu_.sscratch;
            break;
        case csr_addr(Csr::Sepc):
            rcsr = cpu_.sepc;
            break;
        case csr_addr(Csr::Scause):
            rcsr = cpu_.scause;
            break;
        case csr_addr(Csr::Stval):
            rcsr = cpu_.stval;
            break;
        case csr_addr(Csr::Sip):
            rcsr = cpu_.mip & cpu_.mideleg;
            break;
        case csr_addr(Csr::Satp):
            rcsr = cpu_.satp;
            break;

        case csr_addr(Csr::Medeleg):
            rcsr = cpu_.medeleg;
            break;
        case csr_addr(Csr::Mideleg):
            rcsr = cpu_.mideleg;
            break;
        case csr_addr(Csr::Mie):
            rcsr = cpu_.mie;
            break;
        case csr_addr(Csr::Mtvec):
            rcsr = cpu_.mtvec;
            break;
        case csr_addr(Csr::Mcounteren):
            rcsr = cpu_.mcounteren;
            break;
        case csr_addr(Csr::Mscratch):
            rcsr = cpu_.mscratch;
            break;
        case csr_addr(Csr::Mepc):
            rcsr = cpu_.mepc;
            break;
        case csr_addr(Csr::Mcause):
            rcsr = cpu_.mcause;
            break;
        case csr_addr(Csr::Mtval):
            rcsr = cpu_.mtval;
            break;
        case csr_addr(Csr::Mip):
            rcsr = cpu_.mip;
            break;
        case csr_addr(Csr::Misa):
            rcsr = misa_with_mxl(cpu_.misa);
            break;

        case csr_addr(Csr::Mcycle):
        case csr_addr(Csr::Minstret):
        case csr_addr(Csr::Cycle):
        case csr_addr(Csr::Instret):
        case csr_addr(Csr::Time):
            rcsr = static_cast<CSRValue>(cpu_.mtime);
            break;

        case csr_addr(Csr::Mcycleh):
        case csr_addr(Csr::Minstreth):
        case csr_addr(Csr::Cycleh):
        case csr_addr(Csr::Instreth):
        case csr_addr(Csr::Timeh):
            rcsr = static_cast<CSRValue>(cpu_.mtime >> 32);
            break;

        case csr_addr(Csr::Sstatus):
            rcsr = getMstatus(kMstatusSstatusReadMask);
            break;
        case csr_addr(Csr::Mstatus):
            rcsr = getMstatus(kMstatusReadMask);
            break;

        case csr_addr(Csr::Mhartid):
            rcsr = cpu_.mhartid;
            break;

        default:
            break;
    }
    return rcsr;
}

void CsrFile::write(CSRAddress addr, CSRValue wdata) {
    CSRValue mask1 =
        (static_cast<CSRValue>(1) << (enum_mask(ExceptionCode::StorePageFault) + 1)) - 1;
    CSRValue mask2 = enum_mask(MipBit::Ssip) | enum_mask(MipBit::Stip) | enum_mask(MipBit::Seip);
    CSRValue mask3 = enum_mask(MipBit::Msip) | enum_mask(MipBit::Ssip) | enum_mask(MipBit::Stip) |
                     enum_mask(MipBit::Mtip) | enum_mask(MipBit::Seip);
    CSRValue mask4 = enum_mask(MipBit::Ssip) | enum_mask(MipBit::Stip);

    switch (addr) {
        case csr_addr(Csr::Mhartid):
        case csr_addr(Csr::Pmpcfg0):
        case csr_addr(Csr::Pmpaddr0):
        case csr_addr(Csr::Time):
        case csr_addr(Csr::Timeh):
        case csr_addr(Csr::Misa):
            break;

        case csr_addr(Csr::Fflags):
            cpu_.fcsr = (cpu_.fcsr & ~kFflagsMask) | (wdata & kFflagsMask);
            break;
        case csr_addr(Csr::Frm):
            cpu_.fcsr = (cpu_.fcsr & ~(kFrmMask << kFrmShift)) | ((wdata & kFrmMask) << kFrmShift);
            break;
        case csr_addr(Csr::Fcsr):
            cpu_.fcsr = wdata & kFcsrMask;
            break;

        case csr_addr(Csr::Stvec):
            cpu_.stvec = wdata & ~3;
            break;
        case csr_addr(Csr::Scounteren):
            cpu_.scounteren = wdata & 5;
            break;
        case csr_addr(Csr::Sscratch):
            cpu_.sscratch = wdata;
            break;
        case csr_addr(Csr::Sepc):
            cpu_.sepc = wdata & ~1;
            break;
        case csr_addr(Csr::Scause):
            cpu_.scause = wdata;
            break;
        case csr_addr(Csr::Stval):
            cpu_.stval = wdata;
            break;

        case csr_addr(Csr::Mtvec):
            cpu_.mtvec = wdata & ~3;
            break;
        case csr_addr(Csr::Mcounteren):
            cpu_.mcounteren = wdata & 5;
            break;
        case csr_addr(Csr::Mscratch):
            cpu_.mscratch = wdata;
            break;
        case csr_addr(Csr::Mepc):
            cpu_.mepc = wdata & ~1;
            break;
        case csr_addr(Csr::Mcause):
            cpu_.mcause = wdata;
            break;
        case csr_addr(Csr::Mtval):
            cpu_.mtval = wdata;
            break;

        case csr_addr(Csr::Sie):
            cpu_.mie = (cpu_.mie & ~cpu_.mideleg) | (wdata & cpu_.mideleg);
            break;
        case csr_addr(Csr::Sip):
            cpu_.mip = (cpu_.mip & ~cpu_.mideleg) | (wdata & cpu_.mideleg);
            break;
        case csr_addr(Csr::Medeleg):
            cpu_.medeleg = (cpu_.medeleg & ~mask1) | (wdata & mask1);
            break;
        case csr_addr(Csr::Mideleg):
            cpu_.mideleg = (cpu_.mideleg & ~mask2) | (wdata & mask2);
            break;
        case csr_addr(Csr::Mie):
            cpu_.mie = (cpu_.mie & ~mask3) | (wdata & mask3);
            break;
        case csr_addr(Csr::Mip):
            cpu_.mip = (cpu_.mip & ~mask4) | (wdata & mask4);
            break;

        case csr_addr(Csr::Satp):
            cpu_.satp = wdata;
            cpu_.TLB_flush();
            break;

        case csr_addr(Csr::Mstatus):
            setMstatus(wdata);
            break;
        case csr_addr(Csr::Sstatus):
            setMstatus((cpu_.mstatus & ~kSstatusMask) | (wdata & kSstatusMask));
            break;
        default:
            break;
    }
}
