/**
 * @file CsrTypes.hpp
 * @brief CSR register enums, bit masks, and helper functions.
 */
#pragma once

#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

enum class MstatusBit : CSRValue {
    Uie = (1u << 0),
    Sie = (1u << 1),
    Hie = (1u << 2),
    Mie = (1u << 3),
    Upie = (1u << 4),
    Spie = (1u << 5),
    Hpie = (1u << 6),
    Mpie = (1u << 7),
    Spp = (1u << 8),
    Vs = (3u << 9),
    Mpp = (3u << 11),
    Fs = (3u << 13),
    Xs = (3u << 15),
    Mprv = (1u << 17),
    Sum = (1u << 18),
    Mxr = (1u << 19),
    Tvm = (1u << 20),
    Tw = (1u << 21),
    Tsr = (1u << 22),
};

enum class MipBit : CSRValue {
    Usip = (1u << 0),
    Ssip = (1u << 1),
    Hsip = (1u << 2),
    Msip = (1u << 3),
    Utip = (1u << 4),
    Stip = (1u << 5),
    Htip = (1u << 6),
    Mtip = (1u << 7),
    Ueip = (1u << 8),
    Seip = (1u << 9),
    Heip = (1u << 10),
    Meip = (1u << 11),
};

/** Standard interrupt bits implemented by the SimRV single-hart platform. */
constexpr auto interrupt_implemented_mask(bool has_s) -> CSRValue {
    CSRValue mask = enum_mask(MipBit::Msip) | enum_mask(MipBit::Mtip) | enum_mask(MipBit::Meip);
    if (has_s) {
        mask |= enum_mask(MipBit::Ssip) | enum_mask(MipBit::Stip) | enum_mask(MipBit::Seip);
    }
    return mask;
}

/**
 * @brief Writable pending bits in mip for this platform.
 *
 * MSIP, MTIP, and MEIP are read-only signals sourced by CLINT/PLIC. Without
 * Sstc, M-mode may post SSIP, STIP, and the software component of SEIP.
 */
constexpr auto mip_writable_mask(bool has_s) -> CSRValue {
    return has_s ? enum_mask(MipBit::Ssip) | enum_mask(MipBit::Stip) | enum_mask(MipBit::Seip) : 0;
}

/** Supervisor interrupt delegation bits implemented by this platform. */
constexpr auto mideleg_writable_mask(bool has_s) -> CSRValue {
    return has_s ? enum_mask(MipBit::Ssip) | enum_mask(MipBit::Stip) | enum_mask(MipBit::Seip) : 0;
}

/**
 * @brief Produce the CSR read-modify-write base for mip.
 *
 * An architectural mip read reports software-SEIP OR external-SEIP, but CSRRS
 * and CSRRC modify only the software component. This replacement prevents an
 * asserted PLIC line from being accidentally latched into software state.
 */
constexpr auto mip_rmw_base(CSRValue visible_mip, bool seip_software) -> CSRValue {
    visible_mip &= ~enum_mask(MipBit::Seip);
    if (seip_software) visible_mip |= enum_mask(MipBit::Seip);
    return visible_mip;
}

constexpr CSRValue kMstatusMask =
    (enum_mask(MstatusBit::Sie) | enum_mask(MstatusBit::Mie) | enum_mask(MstatusBit::Spie) |
     enum_mask(MstatusBit::Mpie) | enum_mask(MstatusBit::Spp) | enum_mask(MstatusBit::Mpp) |
     enum_mask(MstatusBit::Fs) | enum_mask(MstatusBit::Vs) | enum_mask(MstatusBit::Mprv) |
     enum_mask(MstatusBit::Sum) | enum_mask(MstatusBit::Mxr) | enum_mask(MstatusBit::Tvm) |
     enum_mask(MstatusBit::Tw) | enum_mask(MstatusBit::Tsr) |
     (simrv::xlen::kIsXLen64 ? (static_cast<CSRValue>(0xF) << 32) : 0));
constexpr CSRValue kSstatusMask =
    (enum_mask(MstatusBit::Sie) | enum_mask(MstatusBit::Spie) | enum_mask(MstatusBit::Spp) |
     enum_mask(MstatusBit::Fs) | enum_mask(MstatusBit::Vs) | enum_mask(MstatusBit::Xs) |
     enum_mask(MstatusBit::Sum) | enum_mask(MstatusBit::Mxr) |
     (simrv::xlen::kIsXLen64 ? (static_cast<CSRValue>(0x3) << 32) : 0));
constexpr CSRValue kMstatusFsDirty = enum_mask(MstatusBit::Fs);
constexpr CSRValue kMstatusSd = static_cast<CSRValue>(Word{1} << (simrv::xlen::kXLenBits - 1u));
/** sstatus fields implemented without the obsolete N-extension UIE/UPIE aliases. */
constexpr CSRValue kMstatusSstatusReadMask = kSstatusMask | kMstatusSd;
constexpr CSRValue kMstatusReadMask = static_cast<CSRValue>(kXLenMask);
/**
 * Exception-delegation bits implemented by SimRV: causes 0-8, 12, 13, and 15.
 * S/M ECALL, reserved, hypervisor, and double-trap causes are read-only zero;
 * only exceptions that can originate below S-mode are exposed as delegatable.
 */
constexpr CSRValue kMedelegWritableMask = static_cast<CSRValue>(0xB1FFu);
constexpr CSRValue kFflagsMask = static_cast<CSRValue>(0x1fu);
constexpr CSRValue kFrmMask = static_cast<CSRValue>(0x7u);
constexpr CSRValue kFcsrMask = static_cast<CSRValue>(0xffu);
constexpr unsigned kFrmShift = 5;

/**
 * @brief Synthesize the read-only SD summary bit from extension-state fields.
 *
 * SD is set when FS, VS, or XS is Dirty (binary 11). It is not independent
 * storage and therefore cannot be changed directly by a CSR write.
 */
constexpr auto mstatus_with_sd(CSRValue mstatus) -> CSRValue {
    const bool fs_dirty = (mstatus & enum_mask(MstatusBit::Fs)) == enum_mask(MstatusBit::Fs);
    const bool vs_dirty = (mstatus & enum_mask(MstatusBit::Vs)) == enum_mask(MstatusBit::Vs);
    const bool xs_dirty = (mstatus & enum_mask(MstatusBit::Xs)) == enum_mask(MstatusBit::Xs);
    return (fs_dirty || vs_dirty || xs_dirty) ? (mstatus | kMstatusSd) : (mstatus & ~kMstatusSd);
}

/**
 * @brief Return the writable mstatus fields for the implemented privilege/ISA set.
 *
 * Privileged ISA 1.13 makes supervisor fields read-only zero without S-mode,
 * MPRV/UXL read-only zero without U-mode, and extension-state fields read-only
 * zero when neither the corresponding extension nor S-mode supplies that state.
 * SimRV chooses the permitted read-only-zero behavior for absent F and V state.
 */
constexpr auto mstatus_writable_mask(bool has_s, bool has_u, bool has_f, bool has_v) -> CSRValue {
    CSRValue mask = kMstatusMask;
    if (!has_s) {
        mask &=
            ~(enum_mask(MstatusBit::Sie) | enum_mask(MstatusBit::Spie) |
              enum_mask(MstatusBit::Spp) | enum_mask(MstatusBit::Sum) | enum_mask(MstatusBit::Mxr) |
              enum_mask(MstatusBit::Tvm) | enum_mask(MstatusBit::Tsr));
        if constexpr (simrv::xlen::kIsXLen64) {
            mask &= ~(static_cast<CSRValue>(0x3U) << 34U);  // SXL
        }
    }
    if (!has_u) {
        mask &= ~enum_mask(MstatusBit::Mprv);
        if constexpr (simrv::xlen::kIsXLen64) {
            mask &= ~(static_cast<CSRValue>(0x3U) << 32U);  // UXL
        }
    }
    if (!has_s && !has_u) mask &= ~enum_mask(MstatusBit::Tw);
    if (!has_f) mask &= ~enum_mask(MstatusBit::Fs);
    if (!has_v) mask &= ~enum_mask(MstatusBit::Vs);
    return mask;
}

/** Legalize MPP to a privilege mode implemented by this hart. */
constexpr auto mstatus_legalize_mpp(CSRValue value, bool has_s, bool has_u) -> CSRValue {
    const CSRValue requested = (value & enum_mask(MstatusBit::Mpp)) >> 11U;
    const bool legal = requested == 3U || (requested == 1U && has_s) || (requested == 0U && has_u);
    if (legal) return value;
    return (value & ~enum_mask(MstatusBit::Mpp)) | (CSRValue{3U} << 11U);
}

/** Lowest privilege supported by an M-mode hart, encoded as an MPP value. */
constexpr auto least_supported_mpp(bool has_s, bool has_u) -> CSRValue {
    if (has_u) return 0;
    if (has_s) return 1;
    return 3;
}

/** Project an internal status value into the active architectural CSR width. */
constexpr auto mstatus_read_value(CSRValue mstatus, CSRValue mask, unsigned xlen) -> CSRValue {
    CSRValue value = mstatus_with_sd(mstatus) & mask;
    if constexpr (simrv::xlen::kIsXLen64) {
        if (xlen == 32) {
            const bool sd = (value & kMstatusSd) != 0;
            value = static_cast<CSRValue>(static_cast<uint32_t>(value));
            if (sd) value |= CSRValue{1} << 31U;
        }
    }
    return value;
}

/** Return whether a PMP CSR address exists for the active architectural XLEN. */
constexpr auto pmp_csr_exists(CSRAddress address, unsigned xlen) -> bool {
    if (address >= 0x3A0 && address <= 0x3AF) {
        // RV64 packs eight configuration bytes per even-numbered pmpcfg CSR;
        // the odd-numbered addresses are illegal rather than zero-valued.
        return xlen == 32 || (address & 1U) == 0;
    }
    return address >= 0x3B0 && address <= 0x3EF;
}

/** Debug CSR encodings are unavailable to ordinary U/S/M execution. */
constexpr auto is_debug_csr(CSRAddress address) -> bool {
    return address >= 0x7A0 && address <= 0x7BF;
}

/** Apply architectural privilege, presence, and read-only checks to a CSR encoding. */
constexpr auto csr_access_permitted(PrivilegeLevel current_priv, bool has_s, bool has_u,
                                    CSRAddress address, bool is_write) -> bool {
    if (is_debug_csr(address)) return false;
    const Word required_priv = (address >> 8U) & 0x3U;
    const bool read_only = ((address >> 10U) & 0x3U) == 0x3U;
    if (required_priv == 1U && !has_s) return false;
    if (!has_s && (address == 0x302U || address == 0x303U)) return false;
    if (!has_u && address == 0x306U) return false;
    if (static_cast<Word>(current_priv) < required_priv) return false;
    return !is_write || !read_only;
}

/**
 * @brief Identify implemented hardwired-zero HPM CSRs for the active XLEN.
 *
 * RV32 defines high halves of its 64-bit counters. The same addresses are
 * reserved in RV64 and must not appear as implemented zero-valued CSRs.
 */
constexpr auto is_zero_hpm_csr(CSRAddress address, unsigned xlen) -> bool {
    if ((address >= 0x323U && address <= 0x33FU) || (address >= 0xB03U && address <= 0xB1FU) ||
        (address >= 0xC03U && address <= 0xC1FU)) {
        return true;
    }
    return xlen == 32U &&
           ((address >= 0xB83U && address <= 0xB9FU) || (address >= 0xC83U && address <= 0xC9FU));
}

/**
 * @brief Convert an internal cause value to the active architectural XLEN.
 *
 * RV64 builds can execute an RV32 personality while retaining bit 63 as the
 * internal interrupt marker. Native RV32 builds already use bit 31 and must
 * not attempt that translation.
 */
constexpr auto cause_read_value(CSRValue value, unsigned xlen) -> CSRValue {
    if constexpr (simrv::xlen::kIsXLen64) {
        constexpr CSRValue kInternalInterrupt = static_cast<CSRValue>(uint64_t{1} << 63U);
        if (xlen == 32) {
            const bool interrupt = (value & kInternalInterrupt) != 0;
            value = static_cast<CSRValue>(static_cast<uint32_t>(value));
            if (interrupt) value |= CSRValue{1} << 31U;
        }
    }
    return value;
}

/** Convert a software-written architectural cause to the internal representation. */
constexpr auto cause_write_value(CSRValue value, unsigned xlen) -> CSRValue {
    if constexpr (simrv::xlen::kIsXLen64) {
        constexpr CSRValue kArchitecturalRv32Interrupt = CSRValue{1} << 31U;
        constexpr CSRValue kInternalInterrupt = static_cast<CSRValue>(uint64_t{1} << 63U);
        if (xlen == 32) {
            value = static_cast<CSRValue>(static_cast<uint32_t>(value));
            if ((value & kArchitecturalRv32Interrupt) != 0) {
                return (value & ~kArchitecturalRv32Interrupt) | kInternalInterrupt;
            }
        }
    }
    return value;
}

enum class Csr : CSRAddress {
    // Legacy draft user-interrupt (N) CSR addresses. SimRV does not implement
    // N; accesses to these numeric addresses raise illegal-instruction traps.
    Ustatus = 0x000,
    Uie = 0x004,
    Utvec = 0x005,
    Uscratch = 0x040,
    Uepc = 0x041,
    Ucause = 0x042,
    Utval = 0x043,
    Uip = 0x044,
    Fflags = 0x001,
    Frm = 0x002,
    Fcsr = 0x003,
    Vstart = 0x008,
    Vxsat = 0x009,
    Vxrm = 0x00A,
    Vcsr = 0x00F,
    Pmpcfg0 = 0x3A0,
    Pmpaddr0 = 0x3B0,
    Cycle = 0xC00,
    Time = 0xC01,
    Instret = 0xC02,
    Vl = 0xC20,
    Vtype = 0xC21,
    Vlenb = 0xC22,
    Sstatus = 0x100,
    // Legacy draft N-extension delegation addresses; not implemented.
    Sedeleg = 0x102,
    Sideleg = 0x103,
    Sie = 0x104,
    Stvec = 0x105,
    Scounteren = 0x106,
    Sscratch = 0x140,
    Sepc = 0x141,
    Scause = 0x142,
    Stval = 0x143,
    Sip = 0x144,
    Satp = 0x180,
    Mvendorid = 0xF11,
    Marchid = 0xF12,
    Mimpid = 0xF13,
    Mhartid = 0xF14,
    Mconfigptr = 0xF15,
    Mstatus = 0x300,
    Misa = 0x301,
    Medeleg = 0x302,
    Mideleg = 0x303,
    Mie = 0x304,
    Mtvec = 0x305,
    Mcounteren = 0x306,
    Mscratch = 0x340,
    Mepc = 0x341,
    Mcause = 0x342,
    Mtval = 0x343,
    Mip = 0x344,
    Mcycle = 0xB00,
    Minstret = 0xB02,
    Mcycleh = 0xB80,
    Minstreth = 0xB82,
    Cycleh = 0xC80,
    Timeh = 0xC81,
    Instreth = 0xC82,
};

constexpr CSRAddress csr_addr(Csr csr) { return static_cast<CSRAddress>(csr); }

}  // namespace simrv::core
