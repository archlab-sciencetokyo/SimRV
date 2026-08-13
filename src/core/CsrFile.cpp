/**
 * @file CsrFile.cpp
 * @brief Control and Status Register (CSR) file implementation.
 */
#include "simrv/core/CsrFile.hpp"

#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

namespace {
constexpr unsigned kHighWordShift = 32;
constexpr CSRValue kCounterEnableMask = static_cast<CSRValue>(0x7u);

constexpr auto cause_read_translate(CSRValue value, unsigned xlen) -> CSRValue {
    if (xlen == 32 && (value & (1ULL << 63)) != 0) {
        return (value & ~(1ULL << 63)) | (1ULL << 31);
    }
    return value;
}

constexpr auto cause_write_translate(CSRValue value, unsigned xlen) -> CSRValue {
    if (xlen == 32 && (value & (1ULL << 31)) != 0) {
        return (value & ~(1ULL << 31)) | (1ULL << 63);
    }
    return value;
}
}  // namespace

auto CsrFile::getMstatus(CSRValue mask) const -> CSRValue {
    const CSRValue mstatus = cpu_.state().mstatus;
    const bool fs_dirty = (mstatus & enum_mask(MstatusBit::Fs)) == enum_mask(MstatusBit::Fs);
    const bool xs_dirty = (mstatus & enum_mask(MstatusBit::Xs)) == enum_mask(MstatusBit::Xs);
    const CSRValue val = (fs_dirty || xs_dirty) ? (mstatus | kMstatusSd) : (mstatus & ~kMstatusSd);
    return val & mask;
}

void CsrFile::setMstatus(CSRValue wdata) {
    CSRValue mpp = (wdata & enum_mask(MstatusBit::Mpp)) >> 11;
    if (mpp == 2) {
        wdata &= ~enum_mask(MstatusBit::Mpp);
    }
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
        case csr_addr(Csr::Vstart):
            if (!isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::V) ||
                (cpu_.state().mstatus & enum_mask(MstatusBit::Vs)) == 0) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            rcsr = cpu_.state().vstart;
            break;
        case csr_addr(Csr::Vxsat):
            if (!isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::V) ||
                (cpu_.state().mstatus & enum_mask(MstatusBit::Vs)) == 0) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            rcsr = cpu_.state().vxsat;
            break;
        case csr_addr(Csr::Vxrm):
            if (!isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::V) ||
                (cpu_.state().mstatus & enum_mask(MstatusBit::Vs)) == 0) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            rcsr = cpu_.state().vxrm;
            break;
        case csr_addr(Csr::Vcsr):
            if (!isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::V) ||
                (cpu_.state().mstatus & enum_mask(MstatusBit::Vs)) == 0) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            rcsr = (cpu_.state().vxrm << 1) | cpu_.state().vxsat;
            break;
        case csr_addr(Csr::Vl):
            if (!isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::V) ||
                (cpu_.state().mstatus & enum_mask(MstatusBit::Vs)) == 0) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            rcsr = cpu_.state().vl;
            break;
        case csr_addr(Csr::Vtype):
            if (!isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::V) ||
                (cpu_.state().mstatus & enum_mask(MstatusBit::Vs)) == 0) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            rcsr = cpu_.state().vtype;
            break;
        case csr_addr(Csr::Vlenb):
            if (!isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::V) ||
                (cpu_.state().mstatus & enum_mask(MstatusBit::Vs)) == 0) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            rcsr = 32;  // VLEN=256 bits -> 32 bytes
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
            rcsr = cause_read_translate(cpu_.state().scause, cpu_.state().regs.xlen);
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
            rcsr = cause_read_translate(cpu_.state().mcause, cpu_.state().regs.xlen);
            break;
        case csr_addr(Csr::Mtval):
            rcsr = cpu_.state().mtval;
            break;
        case csr_addr(Csr::Mip):
            rcsr = cpu_.state().mip;
            break;
        case csr_addr(Csr::Misa):
            rcsr = isa::misa_with_mxl(cpu_.state().misa);
            break;

        case csr_addr(Csr::Mcycle):
        case csr_addr(Csr::Cycle):
            rcsr = static_cast<CSRValue>(cpu_.clint_mmio.mcycle);
            break;
        case csr_addr(Csr::Minstret):
        case csr_addr(Csr::Instret):
            rcsr = static_cast<CSRValue>(cpu_.e_icount);
            break;
        case csr_addr(Csr::Time):
            rcsr = static_cast<CSRValue>(cpu_.clint_mmio.mtime);
            break;

        case csr_addr(Csr::Mcycleh):
        case csr_addr(Csr::Cycleh):
            if (cpu_.state().regs.xlen == 64) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            rcsr = static_cast<CSRValue>(cpu_.clint_mmio.mcycle >> kHighWordShift);
            break;
        case csr_addr(Csr::Minstreth):
        case csr_addr(Csr::Instreth):
            if (cpu_.state().regs.xlen == 64) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            rcsr = static_cast<CSRValue>(cpu_.e_icount >> kHighWordShift);
            break;
        case csr_addr(Csr::Timeh):
            if (cpu_.state().regs.xlen == 64) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
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
            if (addr >= 0x3A0 && addr <= 0x3EF) {  // pmpcfg0..15, pmpaddr0..63
                rcsr = 0;
                break;
            }
            if ((addr >= 0x323 && addr <= 0x33F) || (addr >= 0xB03 && addr <= 0xB1F) ||
                (addr >= 0xB83 && addr <= 0xB9F) || (addr >= 0xC03 && addr <= 0xC1F) ||
                (addr >= 0xC83 && addr <= 0xC9F) || (addr >= 0x7A0 && addr <= 0x7AA) ||
                addr == 0x320 || addr == 0x30A || addr == 0x31A || addr == 0x10A ||
                addr == 0x60A || addr == 0x61A) {
                rcsr = 0;
                break;
            }
            return std::unexpected(ExceptionCode::IllegalInstruction);
    }
    return rcsr;
}

auto CsrFile::write(CSRAddress addr, CSRValue wdata)
    -> std::expected<void, ExceptionCode> {  // NOLINT(bugprone-easily-swappable-parameters)
    CSRValue const mask1 =
        (static_cast<CSRValue>(1) << (enum_mask(ExceptionCode::StorePageFault) + 1)) - 1;
    CSRValue const mask2 =
        enum_mask(MipBit::Ssip) | enum_mask(MipBit::Stip) | enum_mask(MipBit::Seip);
    CSRValue const mask3 = enum_mask(MipBit::Msip) | enum_mask(MipBit::Ssip) |
                           enum_mask(MipBit::Stip) | enum_mask(MipBit::Mtip) |
                           enum_mask(MipBit::Seip) | enum_mask(MipBit::Meip);
    CSRValue const mask4 = enum_mask(MipBit::Msip) | enum_mask(MipBit::Ssip);

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

        case csr_addr(Csr::Mcycle):
            if (cpu_.state().regs.xlen == 32) {
                cpu_.clint_mmio.mcycle =
                    (cpu_.clint_mmio.mcycle & 0xFFFFFFFF00000000ULL) | static_cast<uint32_t>(wdata);
            } else {
                cpu_.clint_mmio.mcycle = wdata;
            }
            break;
        case csr_addr(Csr::Mcycleh):
            if (cpu_.state().regs.xlen == 64) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            cpu_.clint_mmio.mcycle = (cpu_.clint_mmio.mcycle & 0x00000000FFFFFFFFULL) |
                                     (static_cast<uint64_t>(wdata) << 32);
            break;
        case csr_addr(Csr::Minstret):
            if (cpu_.state().regs.xlen == 32) {
                cpu_.e_icount =
                    (cpu_.e_icount & 0xFFFFFFFF00000000ULL) | static_cast<uint32_t>(wdata);
            } else {
                cpu_.e_icount = wdata;
            }
            break;
        case csr_addr(Csr::Minstreth):
            if (cpu_.state().regs.xlen == 64) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            cpu_.e_icount =
                (cpu_.e_icount & 0x00000000FFFFFFFFULL) | (static_cast<uint64_t>(wdata) << 32);
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
        case csr_addr(Csr::Vstart):
            if (!isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::V) ||
                (cpu_.state().mstatus & enum_mask(MstatusBit::Vs)) == 0) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            cpu_.state().vstart = wdata & 0x1FF;
            cpu_.state().mstatus |= enum_mask(MstatusBit::Vs);
            break;
        case csr_addr(Csr::Vxsat):
            if (!isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::V) ||
                (cpu_.state().mstatus & enum_mask(MstatusBit::Vs)) == 0) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            cpu_.state().vxsat = wdata & 1;
            cpu_.state().mstatus |= enum_mask(MstatusBit::Vs);
            break;
        case csr_addr(Csr::Vxrm):
            if (!isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::V) ||
                (cpu_.state().mstatus & enum_mask(MstatusBit::Vs)) == 0) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            cpu_.state().vxrm = wdata & 3;
            cpu_.state().mstatus |= enum_mask(MstatusBit::Vs);
            break;
        case csr_addr(Csr::Vcsr):
            if (!isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::V) ||
                (cpu_.state().mstatus & enum_mask(MstatusBit::Vs)) == 0) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            cpu_.state().vxrm = (wdata >> 1) & 3;
            cpu_.state().vxsat = wdata & 1;
            cpu_.state().mstatus |= enum_mask(MstatusBit::Vs);
            break;
        case csr_addr(Csr::Vl):
            if (!isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::V) ||
                (cpu_.state().mstatus & enum_mask(MstatusBit::Vs)) == 0) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            cpu_.state().vl = wdata;
            cpu_.state().mstatus |= enum_mask(MstatusBit::Vs);
            break;
        case csr_addr(Csr::Vtype):
            if (!isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::V) ||
                (cpu_.state().mstatus & enum_mask(MstatusBit::Vs)) == 0) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            cpu_.state().vtype = wdata;
            cpu_.state().mstatus |= enum_mask(MstatusBit::Vs);
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
            cpu_.state().scause = cause_write_translate(wdata, cpu_.state().regs.xlen);
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
            cpu_.state().mcause = cause_write_translate(wdata, cpu_.state().regs.xlen);
            break;
        case csr_addr(Csr::Mtval):
            cpu_.state().mtval = wdata;
            break;

        case csr_addr(Csr::Sie):
            cpu_.state().mie =
                (cpu_.state().mie & ~cpu_.state().mideleg) | (wdata & cpu_.state().mideleg);
            break;
        case csr_addr(Csr::Sip): {
            const CSRValue mask = enum_mask(MipBit::Ssip) & cpu_.state().mideleg;
            cpu_.state().mip = (cpu_.state().mip & ~mask) | (wdata & mask);
            break;
        }
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
                if (cpu_.state().satp != wdata) {
                    cpu_.state().satp = wdata;
                    cpu_.TLB_flush();
                    cpu_.decode_cache.flush();
                }
                // Latch: once translation is enabled it is never "un-seen"
                if (mode != 0) {
                    cpu_.machine_->s_mmu_ever_used = true;
                }
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
            if (addr >= 0x3A0 && addr <= 0x3EF) {  // pmpcfg0..15, pmpaddr0..63
                break;
            }
            if ((addr >= 0x323 && addr <= 0x33F) || (addr >= 0xB03 && addr <= 0xB1F) ||
                (addr >= 0xB83 && addr <= 0xB9F) || (addr >= 0xC03 && addr <= 0xC1F) ||
                (addr >= 0xC83 && addr <= 0xC9F) || (addr >= 0x7A0 && addr <= 0x7AA) ||
                addr == 0x320 || addr == 0x30A || addr == 0x31A || addr == 0x10A ||
                addr == 0x60A || addr == 0x61A) {
                break;
            }
            return std::unexpected(ExceptionCode::IllegalInstruction);
    }
    return {};
}

}  // namespace simrv::core
