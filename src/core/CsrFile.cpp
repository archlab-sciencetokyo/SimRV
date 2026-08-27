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

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace simrv::core {

namespace {
constexpr unsigned kHighWordShift = 32;
constexpr CSRValue kCounterEnableMask = static_cast<CSRValue>(0x7u);

}  // namespace

auto CsrFile::getMstatus(CSRValue mask) const -> CSRValue {
    const CSRValue misa = cpu_.state().misa;
    const CSRValue implemented =
        mstatus_writable_mask(isa::misa_has_extension(misa, isa::IsaExtension::S),
                              isa::misa_has_extension(misa, isa::IsaExtension::U),
                              isa::misa_has_extension(misa, isa::IsaExtension::F),
                              isa::misa_has_extension(misa, isa::IsaExtension::V));
    return mstatus_read_value(cpu_.state().mstatus, mask & (implemented | kMstatusSd),
                              cpu_.state().regs.xlen);
}

void CsrFile::setMstatus(CSRValue wdata) {
    const CSRValue misa = cpu_.state().misa;
    const bool has_s = isa::misa_has_extension(misa, isa::IsaExtension::S);
    const bool has_u = isa::misa_has_extension(misa, isa::IsaExtension::U);
    const bool has_f = isa::misa_has_extension(misa, isa::IsaExtension::F);
    const bool has_v = isa::misa_has_extension(misa, isa::IsaExtension::V);
    wdata = mstatus_legalize_mpp(wdata, has_s, has_u);
    if constexpr (simrv::xlen::kIsXLen64) {
        // SXL and UXL are WARL. SimRV supports 32 and 64 only; reserved
        // encodings retain their prior legal value. An RV32 machine
        // personality fixes both lower modes to RV32.
        const unsigned mxl =
            static_cast<unsigned>((static_cast<uint64_t>(cpu_.state().misa) >> 62U) & 0x3U);
        for (const unsigned shift : {32U, 34U}) {
            const uint64_t field_mask = uint64_t{0x3U} << shift;
            const uint64_t requested = (static_cast<uint64_t>(wdata) >> shift) & 0x3U;
            uint64_t legalized = static_cast<uint64_t>(wdata);
            if (mxl == 1U) {
                legalized = (legalized & ~field_mask) | (uint64_t{1U} << shift);
            } else if (requested != 1U && requested != 2U) {
                legalized = (legalized & ~field_mask) |
                            (static_cast<uint64_t>(cpu_.state().mstatus) & field_mask);
            }
            wdata = static_cast<CSRValue>(legalized);
        }
        const uint64_t sxl = (static_cast<uint64_t>(wdata) >> 34U) & 0x3U;
        const uint64_t uxl = (static_cast<uint64_t>(wdata) >> 32U) & 0x3U;
        if (uxl > sxl) {
            constexpr uint64_t kUxlMask = uint64_t{0x3U} << 32U;
            wdata =
                static_cast<CSRValue>((static_cast<uint64_t>(wdata) & ~kUxlMask) | (sxl << 32U));
        }
    }
    CSRValue const mod = cpu_.state().mstatus ^ wdata;
    const CSRValue tlb_sensitive =
        enum_mask(MstatusBit::Mprv) | enum_mask(MstatusBit::Sum) | enum_mask(MstatusBit::Mxr);
    if ((mod & tlb_sensitive) != 0 ||
        (((cpu_.state().mstatus & enum_mask(MstatusBit::Mprv)) != 0u) &&
         (mod & enum_mask(MstatusBit::Mpp)) != 0)) {
        cpu_.TLB_flush();
    }
    const CSRValue mask = mstatus_writable_mask(has_s, has_u, has_f, has_v);
    cpu_.state().mstatus = (cpu_.state().mstatus & ~mask) | (wdata & mask);
    // Read-only-zero fields must not retain reset/profile state internally,
    // otherwise SD and effective-XLEN calculations could observe absent state.
    cpu_.state().mstatus &= mask;
    cpu_.state().update_xlen();
}

auto CsrFile::read(CSRAddress addr) const -> std::expected<CSRValue, ExceptionCode> {
    CSRValue rcsr = 0;
    switch (addr) {
        case csr_addr(Csr::Fflags):
            if (!isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::F) ||
                (cpu_.state().mstatus & enum_mask(MstatusBit::Fs)) == 0) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            rcsr = cpu_.state().fcsr & kFflagsMask;
            break;
        case csr_addr(Csr::Frm):
            if (!isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::F) ||
                (cpu_.state().mstatus & enum_mask(MstatusBit::Fs)) == 0) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            rcsr = (cpu_.state().fcsr >> kFrmShift) & kFrmMask;
            break;
        case csr_addr(Csr::Fcsr):
            if (!isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::F) ||
                (cpu_.state().mstatus & enum_mask(MstatusBit::Fs)) == 0) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
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
            // vlenb is the read-only architectural VLEN/8 constant for the current hart.
            rcsr = cpu_.state().regs.vlen_bytes();
            break;

        case csr_addr(Csr::Sie):
            rcsr = cpu_.state().mie & cpu_.state().mideleg & interrupt_implemented_mask(true);
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
            rcsr = isa::epc_read_value(cpu_.state().sepc, cpu_.state().misa);
            break;
        case csr_addr(Csr::Scause):
            rcsr = cause_read_value(cpu_.state().scause, cpu_.state().regs.xlen);
            break;
        case csr_addr(Csr::Stval):
            rcsr = cpu_.state().stval;
            break;
        case csr_addr(Csr::Sip):
            rcsr = cpu_.state().mip & cpu_.state().mideleg & interrupt_implemented_mask(true);
            break;
        case csr_addr(Csr::Satp):
            rcsr = cpu_.state().satp;
            break;

        case csr_addr(Csr::Medeleg):
            rcsr = cpu_.state().medeleg;
            break;
        case csr_addr(Csr::Mideleg):
            rcsr = cpu_.state().mideleg & mideleg_writable_mask(true);
            break;
        case csr_addr(Csr::Mie):
            rcsr = cpu_.state().mie & interrupt_implemented_mask(isa::misa_has_extension(
                                          cpu_.state().misa, isa::IsaExtension::S));
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
            rcsr = isa::epc_read_value(cpu_.state().mepc, cpu_.state().misa);
            break;
        case csr_addr(Csr::Mcause):
            rcsr = cause_read_value(cpu_.state().mcause, cpu_.state().regs.xlen);
            break;
        case csr_addr(Csr::Mtval):
            rcsr = cpu_.state().mtval;
            break;
        case csr_addr(Csr::Mip):
            rcsr = cpu_.state().mip & interrupt_implemented_mask(isa::misa_has_extension(
                                          cpu_.state().misa, isa::IsaExtension::S));
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
            if (pmp_csr_exists(addr, cpu_.state().regs.xlen)) {
                if (addr >= 0x3A0 && addr <= 0x3AF) {
                    const size_t cfg_idx = addr - 0x3A0;
                    if (cpu_.state().regs.xlen == 32) {
                        if (cfg_idx < 4) {
                            rcsr =
                                static_cast<CSRValue>(cpu_.state().pmpcfg[cfg_idx * 4]) |
                                (static_cast<CSRValue>(cpu_.state().pmpcfg[cfg_idx * 4 + 1]) << 8) |
                                (static_cast<CSRValue>(cpu_.state().pmpcfg[cfg_idx * 4 + 2])
                                 << 16) |
                                (static_cast<CSRValue>(cpu_.state().pmpcfg[cfg_idx * 4 + 3]) << 24);
                        } else {
                            rcsr = 0;
                        }
                    } else {
                        if (cfg_idx < 4 && (cfg_idx & 1) == 0) {
                            const size_t base_pmp = (cfg_idx / 2) * 8;
                            rcsr = 0;
                            for (size_t b = 0; b < 8; ++b) {
                                if (base_pmp + b < ArchState::kNumPmpEntries) {
                                    rcsr |= static_cast<CSRValue>(cpu_.state().pmpcfg[base_pmp + b])
                                            << (8 * b);
                                }
                            }
                        } else {
                            rcsr = 0;
                        }
                    }
                } else if (addr >= 0x3B0 && addr <= 0x3EF) {
                    const size_t pmp_idx = addr - 0x3B0;
                    if (pmp_idx < ArchState::kNumPmpEntries) {
                        rcsr = static_cast<CSRValue>(cpu_.state().pmpaddr[pmp_idx]);
                    } else {
                        rcsr = 0;
                    }
                } else {
                    rcsr = 0;
                }
                break;
            }
            if (is_zero_hpm_csr(addr, cpu_.state().regs.xlen) || addr == 0x320) {
                rcsr = 0;
                break;
            }
            return std::unexpected(ExceptionCode::IllegalInstruction);
    }
    return rcsr;
}

auto CsrFile::write(CSRAddress addr, CSRValue wdata)
    -> std::expected<void, ExceptionCode> {  // NOLINT(bugprone-easily-swappable-parameters)
    const bool has_s = isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::S);
    const CSRValue interrupt_mask = interrupt_implemented_mask(has_s);

    switch (addr) {
        case csr_addr(Csr::Mvendorid):
        case csr_addr(Csr::Marchid):
        case csr_addr(Csr::Mimpid):
        case csr_addr(Csr::Mconfigptr):
        case csr_addr(Csr::Mhartid):
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
            if (!isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::F) ||
                (cpu_.state().mstatus & enum_mask(MstatusBit::Fs)) == 0) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            cpu_.state().fcsr = (cpu_.state().fcsr & ~kFflagsMask) | (wdata & kFflagsMask);
            cpu_.state().mstatus |= enum_mask(MstatusBit::Fs);
#if defined(__x86_64__) || defined(_M_X64)
            _mm_setcsr(_mm_getcsr() & ~0x3fu);
#endif
            break;
        case csr_addr(Csr::Frm):
            if (!isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::F) ||
                (cpu_.state().mstatus & enum_mask(MstatusBit::Fs)) == 0) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            cpu_.state().fcsr =
                (cpu_.state().fcsr & ~(kFrmMask << kFrmShift)) | ((wdata & kFrmMask) << kFrmShift);
            cpu_.state().mstatus |= enum_mask(MstatusBit::Fs);
            break;
        case csr_addr(Csr::Fcsr):
            if (!isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::F) ||
                (cpu_.state().mstatus & enum_mask(MstatusBit::Fs)) == 0) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            cpu_.state().fcsr = wdata & kFcsrMask;
            cpu_.state().mstatus |= enum_mask(MstatusBit::Fs);
#if defined(__x86_64__) || defined(_M_X64)
            _mm_setcsr(_mm_getcsr() & ~0x3fu);
#endif
            break;
        case csr_addr(Csr::Vstart):
            if (!isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::V) ||
                (cpu_.state().mstatus & enum_mask(MstatusBit::Vs)) == 0) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            cpu_.state().vstart = wdata & cpu_.state().regs.vstart_mask();
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
            cpu_.state().scause = cause_write_value(wdata, cpu_.state().regs.xlen);
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
            cpu_.state().mcause = cause_write_value(wdata, cpu_.state().regs.xlen);
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
            cpu_.state().medeleg = wdata & kMedelegWritableMask;
            break;
        case csr_addr(Csr::Mideleg):
            cpu_.state().mideleg = wdata & mideleg_writable_mask(has_s);
            break;
        case csr_addr(Csr::Mie):
            cpu_.state().mie = wdata & interrupt_mask;
            break;
        case csr_addr(Csr::Mip): {
            const CSRValue writable = mip_writable_mask(has_s);
            const CSRValue sourced = enum_mask(MipBit::Seip) | enum_mask(MipBit::Stip);
            const CSRValue ordinary = writable & ~sourced;
            cpu_.state().mip = (cpu_.state().mip & ~ordinary) | (wdata & ordinary);
            cpu_.state().seip_software = (wdata & writable & enum_mask(MipBit::Seip)) != 0;
            cpu_.state().stip_software = (wdata & writable & enum_mask(MipBit::Stip)) != 0;
            cpu_.state().refresh_supervisor_pending();
            break;
        }

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
            if (pmp_csr_exists(addr, cpu_.state().regs.xlen)) {
                if (addr >= 0x3A0 && addr <= 0x3AF) {
                    const size_t cfg_idx = addr - 0x3A0;
                    if (cpu_.state().regs.xlen == 32) {
                        if (cfg_idx < 4) {
                            for (size_t b = 0; b < 4; ++b) {
                                const size_t entry = cfg_idx * 4 + b;
                                if (entry < ArchState::kNumPmpEntries) {
                                    if ((cpu_.state().pmpcfg[entry] & 0x80) == 0) {
                                        cpu_.state().pmpcfg[entry] =
                                            static_cast<uint8_t>((wdata >> (8 * b)) & 0xFF);
                                    }
                                }
                            }
                        }
                    } else {
                        if (cfg_idx < 4 && (cfg_idx & 1) == 0) {
                            const size_t base_pmp = (cfg_idx / 2) * 8;
                            for (size_t b = 0; b < 8; ++b) {
                                const size_t entry = base_pmp + b;
                                if (entry < ArchState::kNumPmpEntries) {
                                    if ((cpu_.state().pmpcfg[entry] & 0x80) == 0) {
                                        cpu_.state().pmpcfg[entry] =
                                            static_cast<uint8_t>((wdata >> (8 * b)) & 0xFF);
                                    }
                                }
                            }
                        }
                    }
                } else if (addr >= 0x3B0 && addr <= 0x3EF) {
                    const size_t pmp_idx = addr - 0x3B0;
                    if (pmp_idx < ArchState::kNumPmpEntries) {
                        const bool self_locked = (cpu_.state().pmpcfg[pmp_idx] & 0x80) != 0;
                        const bool next_locked_tor =
                            (pmp_idx + 1 < ArchState::kNumPmpEntries) &&
                            ((cpu_.state().pmpcfg[pmp_idx + 1] & 0x80) != 0) &&
                            ((cpu_.state().pmpcfg[pmp_idx + 1] & 0x18) == 0x08);
                        if (!self_locked && !next_locked_tor) {
                            cpu_.state().pmpaddr[pmp_idx] = static_cast<Address>(wdata);
                        }
                    }
                }
                cpu_.state().refresh_pmp_status();
                cpu_.TLB_flush();
                break;
            }
            if (is_zero_hpm_csr(addr, cpu_.state().regs.xlen) || addr == 0x320) {
                break;
            }
            return std::unexpected(ExceptionCode::IllegalInstruction);
    }
    return {};
}

}  // namespace simrv::core
