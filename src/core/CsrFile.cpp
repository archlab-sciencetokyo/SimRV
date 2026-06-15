/**
 * @file CsrFile.cpp
 * @brief SimRV implementation unit.
 */
#include "simrv/core/CsrFile.hpp"

#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

namespace {
constexpr unsigned kHighWordShift = 32;
constexpr CSRValue kCounterEnableMask = static_cast<CSRValue>(0x7u);
}  // namespace

auto CsrFile::getMstatus(CSRValue mask) const -> CSRValue {
    const CSRValue mstatus = cpu_.state().mstatus;
    const bool fs_dirty = (mstatus & enum_mask(MstatusBit::Fs)) == enum_mask(MstatusBit::Fs);
    const bool xs_dirty = (mstatus & enum_mask(MstatusBit::Xs)) == enum_mask(MstatusBit::Xs);
    const CSRValue val = (fs_dirty || xs_dirty) ? (mstatus | kMstatusSd) : (mstatus & ~kMstatusSd);
    return val & mask;
}

void CsrFile::setMstatus(CSRValue wdata) {
    CSRValue const mod = cpu_.state().mstatus ^ wdata;
    const CSRValue tlb_sensitive =
        enum_mask(MstatusBit::Mprv) | enum_mask(MstatusBit::Sum) | enum_mask(MstatusBit::Mxr);
    if ((mod & tlb_sensitive) != 0 ||
        (((cpu_.state().mstatus & enum_mask(MstatusBit::Mprv)) != 0u) &&
         (mod & enum_mask(MstatusBit::Mpp)) != 0)) {
        cpu_.TLB_flush();
    }
    CSRValue const mask = kMstatusMask;
    cpu_.state().mstatus = (cpu_.state().mstatus & ~mask) | (wdata & mask);
    cpu_.state().update_xlen();
}

auto CsrFile::read(CSRAddress addr) const -> std::expected<CSRValue, ExceptionCode> {
    CSRValue rcsr = 0;
    switch (addr) {
        case csr_addr(Csr::Pmpcfg0):
        case csr_addr(Csr::Pmpaddr0):
            rcsr = 0;
            break;

        case csr_addr(Csr::Fflags):
            rcsr = cpu_.state().fcsr & kFflagsMask;
            break;
        case csr_addr(Csr::Frm):
            rcsr = (cpu_.state().fcsr >> kFrmShift) & kFrmMask;
            break;
        case csr_addr(Csr::Fcsr):
            rcsr = cpu_.state().fcsr & kFcsrMask;
            break;

        case csr_addr(Csr::Sie):
            rcsr = cpu_.state().mie & cpu_.state().mideleg;
            break;
        case csr_addr(Csr::Stvec):
            rcsr = cpu_.state().stvec;
            break;
        case csr_addr(Csr::Scounteren):
            rcsr = cpu_.state().scounteren;
            break;
        case csr_addr(Csr::Sscratch):
            rcsr = cpu_.state().sscratch;
            break;
        case csr_addr(Csr::Sepc):
            rcsr = cpu_.state().sepc;
            break;
        case csr_addr(Csr::Scause):
            rcsr = cpu_.state().scause;
            if (cpu_.state().regs.xlen == 32 && (rcsr & (1ULL << 63)) != 0) {
                rcsr = (rcsr & ~(1ULL << 63)) | (1ULL << 31);
            }
            break;
        case csr_addr(Csr::Stval):
            rcsr = cpu_.state().stval;
            break;
        case csr_addr(Csr::Sip):
            rcsr = cpu_.state().mip & cpu_.state().mideleg;
            break;
        case csr_addr(Csr::Satp):
            rcsr = cpu_.state().satp;
            break;
 
        case csr_addr(Csr::Medeleg):
            rcsr = cpu_.state().medeleg;
            break;
        case csr_addr(Csr::Mideleg):
            rcsr = cpu_.state().mideleg;
            break;
        case csr_addr(Csr::Mie):
            rcsr = cpu_.state().mie;
            break;
        case csr_addr(Csr::Mtvec):
            rcsr = cpu_.state().mtvec;
            break;
        case csr_addr(Csr::Mcounteren):
            rcsr = cpu_.state().mcounteren;
            break;
        case csr_addr(Csr::Mscratch):
            rcsr = cpu_.state().mscratch;
            break;
        case csr_addr(Csr::Mepc):
            rcsr = cpu_.state().mepc;
            break;
        case csr_addr(Csr::Mcause):
            rcsr = cpu_.state().mcause;
            if (cpu_.state().regs.xlen == 32 && (rcsr & (1ULL << 63)) != 0) {
                rcsr = (rcsr & ~(1ULL << 63)) | (1ULL << 31);
            }
            break;
        case csr_addr(Csr::Mtval):
            rcsr = cpu_.state().mtval;
            break;
        case csr_addr(Csr::Mip):
            rcsr = cpu_.state().mip;
            break;
        case csr_addr(Csr::Misa):
            rcsr = misa_with_mxl(cpu_.state().misa);
            break;

        case csr_addr(Csr::Mcycle):
        case csr_addr(Csr::Minstret):
        case csr_addr(Csr::Cycle):
        case csr_addr(Csr::Instret):
            rcsr = static_cast<CSRValue>(cpu_.clint_mmio.mcycle);
            break;
        case csr_addr(Csr::Time):
            rcsr = static_cast<CSRValue>(cpu_.clint_mmio.mtime);
            break;

        case csr_addr(Csr::Mcycleh):
        case csr_addr(Csr::Minstreth):
        case csr_addr(Csr::Cycleh):
        case csr_addr(Csr::Instreth):
            rcsr = static_cast<CSRValue>(cpu_.clint_mmio.mcycle >> kHighWordShift);
            break;
        case csr_addr(Csr::Timeh):
            rcsr = static_cast<CSRValue>(cpu_.clint_mmio.mtime >> kHighWordShift);
            break;

        case csr_addr(Csr::Sstatus):
            rcsr = getMstatus(kMstatusSstatusReadMask);
            break;
        case csr_addr(Csr::Mstatus):
            rcsr = getMstatus(kMstatusReadMask);
            break;

        case csr_addr(Csr::Mvendorid):
        case csr_addr(Csr::Marchid):
        case csr_addr(Csr::Mimpid):
        case csr_addr(Csr::Mconfigptr):
            rcsr = 0;
            break;

        case csr_addr(Csr::Mhartid):
            rcsr = cpu_.state().mhartid;
            break;

        default:
            return std::unexpected(ExceptionCode::IllegalInstruction);
    }
    return rcsr;
}

auto CsrFile::write(CSRAddress addr,
                    CSRValue wdata) -> std::expected<void, ExceptionCode> {  // NOLINT(bugprone-easily-swappable-parameters)
    CSRValue const mask1 =
        (static_cast<CSRValue>(1) << (enum_mask(ExceptionCode::StorePageFault) + 1)) - 1;
    CSRValue const mask2 =
        enum_mask(MipBit::Ssip) | enum_mask(MipBit::Stip) | enum_mask(MipBit::Seip);
    CSRValue const mask3 = enum_mask(MipBit::Msip) | enum_mask(MipBit::Ssip) |
                           enum_mask(MipBit::Stip) | enum_mask(MipBit::Mtip) |
                           enum_mask(MipBit::Seip);
    CSRValue const mask4 = enum_mask(MipBit::Msip) | enum_mask(MipBit::Ssip) |
                           enum_mask(MipBit::Mtip) | enum_mask(MipBit::Stip) |
                           enum_mask(MipBit::Meip) | enum_mask(MipBit::Seip);

    switch (addr) {
        case csr_addr(Csr::Mvendorid):
        case csr_addr(Csr::Marchid):
        case csr_addr(Csr::Mimpid):
        case csr_addr(Csr::Mconfigptr):
        case csr_addr(Csr::Mhartid):
        case csr_addr(Csr::Pmpcfg0):
        case csr_addr(Csr::Pmpaddr0):
        case csr_addr(Csr::Time):
        case csr_addr(Csr::Timeh):
        case csr_addr(Csr::Misa):
            break;

        case csr_addr(Csr::Fflags):
            cpu_.state().fcsr = (cpu_.state().fcsr & ~kFflagsMask) | (wdata & kFflagsMask);
            cpu_.state().mstatus |= enum_mask(MstatusBit::Fs);
            break;
        case csr_addr(Csr::Frm):
            cpu_.state().fcsr =
                (cpu_.state().fcsr & ~(kFrmMask << kFrmShift)) | ((wdata & kFrmMask) << kFrmShift);
            cpu_.state().mstatus |= enum_mask(MstatusBit::Fs);
            break;
        case csr_addr(Csr::Fcsr):
            cpu_.state().fcsr = wdata & kFcsrMask;
            cpu_.state().mstatus |= enum_mask(MstatusBit::Fs);
            break;

        case csr_addr(Csr::Stvec):
            cpu_.state().stvec = wdata & ~static_cast<CSRValue>(2);
            break;
        case csr_addr(Csr::Scounteren):
            cpu_.state().scounteren = wdata & kCounterEnableMask;
            break;
        case csr_addr(Csr::Sscratch):
            cpu_.state().sscratch = wdata;
            break;
        case csr_addr(Csr::Sepc):
            cpu_.state().sepc = wdata & ~1;
            break;
        case csr_addr(Csr::Scause):
            if (cpu_.state().regs.xlen == 32 && (wdata & (1ULL << 31)) != 0) {
                cpu_.state().scause = (wdata & ~(1ULL << 31)) | (1ULL << 63);
            } else {
                cpu_.state().scause = wdata;
            }
            break;
        case csr_addr(Csr::Stval):
            cpu_.state().stval = wdata;
            break;

        case csr_addr(Csr::Mtvec):
            cpu_.state().mtvec = wdata & ~static_cast<CSRValue>(2);
            break;
        case csr_addr(Csr::Mcounteren):
            cpu_.state().mcounteren = wdata & kCounterEnableMask;
            break;
        case csr_addr(Csr::Mscratch):
            cpu_.state().mscratch = wdata;
            break;
        case csr_addr(Csr::Mepc):
            cpu_.state().mepc = wdata & ~1;
            break;
        case csr_addr(Csr::Mcause):
            if (cpu_.state().regs.xlen == 32 && (wdata & (1ULL << 31)) != 0) {
                cpu_.state().mcause = (wdata & ~(1ULL << 31)) | (1ULL << 63);
            } else {
                cpu_.state().mcause = wdata;
            }
            break;
        case csr_addr(Csr::Mtval):
            cpu_.state().mtval = wdata;
            break;

        case csr_addr(Csr::Sie):
            cpu_.state().mie =
                (cpu_.state().mie & ~cpu_.state().mideleg) | (wdata & cpu_.state().mideleg);
            break;
        case csr_addr(Csr::Sip):
            cpu_.state().mip =
                (cpu_.state().mip & ~cpu_.state().mideleg) | (wdata & cpu_.state().mideleg);
            break;
        case csr_addr(Csr::Medeleg):
            cpu_.state().medeleg = (cpu_.state().medeleg & ~mask1) | (wdata & mask1);
            break;
        case csr_addr(Csr::Mideleg):
            cpu_.state().mideleg = (cpu_.state().mideleg & ~mask2) | (wdata & mask2);
            break;
        case csr_addr(Csr::Mie):
            cpu_.state().mie = (cpu_.state().mie & ~mask3) | (wdata & mask3);
            break;
        case csr_addr(Csr::Mip):
            cpu_.state().mip = (cpu_.state().mip & ~mask4) | (wdata & mask4);
            break;

        case csr_addr(Csr::Satp): {
            const unsigned xlen = cpu_.state().regs.xlen;
            const Word mode = simrv::xlen::satp_mode(wdata, xlen);
            if (simrv::xlen::satp_mode_supported(mode, xlen)) {
                cpu_.state().satp = wdata;
            }
            break;
        }

        case csr_addr(Csr::Mstatus):
            setMstatus(wdata);
            break;
        case csr_addr(Csr::Sstatus):
            setMstatus((cpu_.state().mstatus & ~kSstatusMask) | (wdata & kSstatusMask));
            break;
        default:
            return std::unexpected(ExceptionCode::IllegalInstruction);
    }
    return {};
}

}  // namespace simrv::core
