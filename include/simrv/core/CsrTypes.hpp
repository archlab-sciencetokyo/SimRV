/**
 * @file CsrTypes.hpp
 * @brief CSR register enums, bit masks, and helper functions.
 */
#pragma once

#include "simrv/xlen/IdentifierTypes.hpp"

namespace simrv::core {

enum class MstatusBit : simrv::xlen::CsrValue {
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
    Gva = static_cast<simrv::xlen::CsrValue>(uint64_t{1} << 38U),
    Mpv = static_cast<simrv::xlen::CsrValue>(uint64_t{1} << 39U),
};

constexpr auto operator|(MstatusBit lhs, MstatusBit rhs) noexcept -> simrv::xlen::CsrValue {
    return std::to_underlying(lhs) | std::to_underlying(rhs);
}
constexpr auto operator&(MstatusBit lhs, MstatusBit rhs) noexcept -> simrv::xlen::CsrValue {
    return std::to_underlying(lhs) & std::to_underlying(rhs);
}
constexpr auto operator~(MstatusBit bit) noexcept -> simrv::xlen::CsrValue {
    return ~std::to_underlying(bit);
}
constexpr auto operator|(simrv::xlen::CsrValue lhs, MstatusBit rhs) noexcept
    -> simrv::xlen::CsrValue {
    return lhs | std::to_underlying(rhs);
}
constexpr auto operator|(MstatusBit lhs, simrv::xlen::CsrValue rhs) noexcept
    -> simrv::xlen::CsrValue {
    return std::to_underlying(lhs) | rhs;
}
constexpr auto operator&(simrv::xlen::CsrValue lhs, MstatusBit rhs) noexcept
    -> simrv::xlen::CsrValue {
    return lhs & std::to_underlying(rhs);
}
constexpr auto operator&(MstatusBit lhs, simrv::xlen::CsrValue rhs) noexcept
    -> simrv::xlen::CsrValue {
    return std::to_underlying(lhs) & rhs;
}
constexpr auto operator^(MstatusBit lhs, MstatusBit rhs) noexcept -> simrv::xlen::CsrValue {
    return std::to_underlying(lhs) ^ std::to_underlying(rhs);
}
constexpr auto operator^(simrv::xlen::CsrValue lhs, MstatusBit rhs) noexcept
    -> simrv::xlen::CsrValue {
    return lhs ^ std::to_underlying(rhs);
}
constexpr auto operator^(MstatusBit lhs, simrv::xlen::CsrValue rhs) noexcept
    -> simrv::xlen::CsrValue {
    return std::to_underlying(lhs) ^ rhs;
}
constexpr auto operator^=(simrv::xlen::CsrValue& lhs, MstatusBit rhs) noexcept
    -> simrv::xlen::CsrValue& {
    lhs ^= std::to_underlying(rhs);
    return lhs;
}
constexpr auto operator|=(simrv::xlen::CsrValue& lhs, MstatusBit rhs) noexcept
    -> simrv::xlen::CsrValue& {
    lhs |= std::to_underlying(rhs);
    return lhs;
}
constexpr auto operator&=(simrv::xlen::CsrValue& lhs, MstatusBit rhs) noexcept
    -> simrv::xlen::CsrValue& {
    lhs &= std::to_underlying(rhs);
    return lhs;
}
template <std::integral I>
constexpr auto operator==(I lhs, MstatusBit rhs) noexcept -> bool {
    return lhs == static_cast<I>(std::to_underlying(rhs));
}
template <std::integral I>
constexpr auto operator==(MstatusBit lhs, I rhs) noexcept -> bool {
    return static_cast<I>(std::to_underlying(lhs)) == rhs;
}
template <std::integral I>
constexpr auto operator!=(I lhs, MstatusBit rhs) noexcept -> bool {
    return !(lhs == rhs);
}
template <std::integral I>
constexpr auto operator!=(MstatusBit lhs, I rhs) noexcept -> bool {
    return !(lhs == rhs);
}

enum class MipBit : simrv::xlen::CsrValue {
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
    Lcofip = (1u << 13),
};

constexpr auto operator|(MipBit lhs, MipBit rhs) noexcept -> simrv::xlen::CsrValue {
    return std::to_underlying(lhs) | std::to_underlying(rhs);
}
constexpr auto operator&(MipBit lhs, MipBit rhs) noexcept -> simrv::xlen::CsrValue {
    return std::to_underlying(lhs) & std::to_underlying(rhs);
}
constexpr auto operator~(MipBit bit) noexcept -> simrv::xlen::CsrValue {
    return ~std::to_underlying(bit);
}
constexpr auto operator|(simrv::xlen::CsrValue lhs, MipBit rhs) noexcept -> simrv::xlen::CsrValue {
    return lhs | std::to_underlying(rhs);
}
constexpr auto operator|(MipBit lhs, simrv::xlen::CsrValue rhs) noexcept -> simrv::xlen::CsrValue {
    return std::to_underlying(lhs) | rhs;
}
constexpr auto operator&(simrv::xlen::CsrValue lhs, MipBit rhs) noexcept -> simrv::xlen::CsrValue {
    return lhs & std::to_underlying(rhs);
}
constexpr auto operator&(MipBit lhs, simrv::xlen::CsrValue rhs) noexcept -> simrv::xlen::CsrValue {
    return std::to_underlying(lhs) & rhs;
}
constexpr auto operator^(MipBit lhs, MipBit rhs) noexcept -> simrv::xlen::CsrValue {
    return std::to_underlying(lhs) ^ std::to_underlying(rhs);
}
constexpr auto operator^(simrv::xlen::CsrValue lhs, MipBit rhs) noexcept -> simrv::xlen::CsrValue {
    return lhs ^ std::to_underlying(rhs);
}
constexpr auto operator^(MipBit lhs, simrv::xlen::CsrValue rhs) noexcept -> simrv::xlen::CsrValue {
    return std::to_underlying(lhs) ^ rhs;
}
constexpr auto operator^=(simrv::xlen::CsrValue& lhs, MipBit rhs) noexcept
    -> simrv::xlen::CsrValue& {
    lhs ^= std::to_underlying(rhs);
    return lhs;
}
constexpr auto operator|=(simrv::xlen::CsrValue& lhs, MipBit rhs) noexcept
    -> simrv::xlen::CsrValue& {
    lhs |= std::to_underlying(rhs);
    return lhs;
}
constexpr auto operator&=(simrv::xlen::CsrValue& lhs, MipBit rhs) noexcept
    -> simrv::xlen::CsrValue& {
    lhs &= std::to_underlying(rhs);
    return lhs;
}
template <std::integral I>
constexpr auto operator==(I lhs, MipBit rhs) noexcept -> bool {
    return lhs == static_cast<I>(std::to_underlying(rhs));
}
template <std::integral I>
constexpr auto operator==(MipBit lhs, I rhs) noexcept -> bool {
    return static_cast<I>(std::to_underlying(lhs)) == rhs;
}
template <std::integral I>
constexpr auto operator!=(I lhs, MipBit rhs) noexcept -> bool {
    return !(lhs == rhs);
}
template <std::integral I>
constexpr auto operator!=(MipBit lhs, I rhs) noexcept -> bool {
    return !(lhs == rhs);
}

/** Standard interrupt bits implemented by the SimRV single-hart platform. */
constexpr auto interrupt_implemented_mask(bool has_s) -> simrv::xlen::CsrValue {
    simrv::xlen::CsrValue mask = MipBit::Msip | MipBit::Mtip | MipBit::Meip;
    if (has_s) {
        mask |= MipBit::Ssip | MipBit::Stip | MipBit::Seip | MipBit::Lcofip;
    }
    return mask;
}

/**
 * @brief Writable pending bits in mip for this platform.
 *
 * MSIP, MTIP, and MEIP are read-only signals sourced by CLINT/PLIC. Without
 * Sstc, M-mode may post SSIP, STIP, and the software component of SEIP.
 */
constexpr auto mip_writable_mask(bool has_s) -> simrv::xlen::CsrValue {
    return has_s ? (MipBit::Ssip | MipBit::Stip | MipBit::Seip | MipBit::Lcofip) : 0;
}

/** Supervisor interrupt delegation bits implemented by this platform. */
constexpr auto mideleg_writable_mask(bool has_s) -> simrv::xlen::CsrValue {
    return has_s ? (MipBit::Ssip | MipBit::Stip | MipBit::Seip | MipBit::Lcofip) : 0;
}

/**
 * @brief Produce the CSR read-modify-write base for mip.
 *
 * An architectural mip read reports software-SEIP OR external-SEIP, but CSRRS
 * and CSRRC modify only the software component. This replacement prevents an
 * asserted PLIC line from being accidentally latched into software state.
 */
constexpr auto mip_rmw_base(simrv::xlen::CsrValue visible_mip, bool seip_software)
    -> simrv::xlen::CsrValue {
    visible_mip &= ~MipBit::Seip;
    if (seip_software) visible_mip |= MipBit::Seip;
    return visible_mip;
}

constexpr simrv::xlen::CsrValue kMstatusMask =
    (MstatusBit::Sie | MstatusBit::Mie | MstatusBit::Spie | MstatusBit::Mpie | MstatusBit::Spp |
     MstatusBit::Mpp | MstatusBit::Fs | MstatusBit::Vs | MstatusBit::Mprv | MstatusBit::Sum |
     MstatusBit::Mxr | MstatusBit::Tvm | MstatusBit::Tw | MstatusBit::Tsr |
     MstatusBit::Mpv |
     (simrv::xlen::kIsXLen64 ? (static_cast<simrv::xlen::CsrValue>(0xF) << 32) : 0));
constexpr simrv::xlen::CsrValue kSstatusMask =
    (MstatusBit::Sie | MstatusBit::Spie | MstatusBit::Spp | MstatusBit::Fs | MstatusBit::Vs |
     MstatusBit::Xs | MstatusBit::Sum | MstatusBit::Mxr |
     (simrv::xlen::kIsXLen64 ? (static_cast<simrv::xlen::CsrValue>(0x3) << 32) : 0));
constexpr simrv::xlen::CsrValue kMstatusFsDirty = std::to_underlying(MstatusBit::Fs);
constexpr simrv::xlen::CsrValue kMstatusGva = std::to_underlying(MstatusBit::Gva);
constexpr simrv::xlen::CsrValue kMstatusSd =
    static_cast<simrv::xlen::CsrValue>(simrv::xlen::Word{1} << (simrv::xlen::kXLenBits - 1u));
/** sstatus fields implemented without the obsolete N-extension UIE/UPIE aliases. */
constexpr simrv::xlen::CsrValue kMstatusSstatusReadMask = kSstatusMask | kMstatusSd;
constexpr simrv::xlen::CsrValue kMstatusReadMask = ~simrv::xlen::CsrValue{0};
/**
 * Exception-delegation bits implemented by SimRV: causes 0-8, 12, 13, and 15.
 * S/M ECALL, reserved, hypervisor, and double-trap causes are read-only zero;
 * only exceptions that can originate below S-mode are exposed as delegatable.
 */
constexpr simrv::xlen::CsrValue kMedelegWritableMask = static_cast<simrv::xlen::CsrValue>(0xB1FFu);
constexpr simrv::xlen::CsrValue kFflagsMask = static_cast<simrv::xlen::CsrValue>(0x1fu);
constexpr simrv::xlen::CsrValue kFrmMask = static_cast<simrv::xlen::CsrValue>(0x7u);
constexpr simrv::xlen::CsrValue kFcsrMask = static_cast<simrv::xlen::CsrValue>(0xffu);
constexpr unsigned kFrmShift = 5;

/**
 * @brief Synthesize the read-only SD summary bit from extension-state fields.
 *
 * SD is set when FS, VS, or XS is Dirty (binary 11). It is not independent
 * storage and therefore cannot be changed directly by a CSR write.
 */
constexpr auto mstatus_with_sd(simrv::xlen::CsrValue mstatus) -> simrv::xlen::CsrValue {
    const bool fs_dirty = (mstatus & MstatusBit::Fs) == MstatusBit::Fs;
    const bool vs_dirty = (mstatus & MstatusBit::Vs) == MstatusBit::Vs;
    const bool xs_dirty = (mstatus & MstatusBit::Xs) == MstatusBit::Xs;
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
constexpr auto mstatus_writable_mask(bool has_s, bool has_u, bool has_f, bool has_v,
                                     bool has_h = false)
    -> simrv::xlen::CsrValue {
    simrv::xlen::CsrValue mask = kMstatusMask;
    if (!has_s) {
        mask &= ~(MstatusBit::Sie | MstatusBit::Spie | MstatusBit::Spp | MstatusBit::Sum |
                  MstatusBit::Mxr | MstatusBit::Tvm | MstatusBit::Tsr);
        if constexpr (simrv::xlen::kIsXLen64) {
            mask &= ~(static_cast<simrv::xlen::CsrValue>(0x3U) << 34U);  // SXL
        }
    }
    if (!has_u) {
        mask &= ~MstatusBit::Mprv;
        if constexpr (simrv::xlen::kIsXLen64) {
            mask &= ~(static_cast<simrv::xlen::CsrValue>(0x3U) << 32U);  // UXL
        }
    }
    if (!has_s && !has_u) mask &= ~MstatusBit::Tw;
    if (!has_f) mask &= ~MstatusBit::Fs;
    if (!has_v) mask &= ~MstatusBit::Vs;
    if (!has_h) mask &= ~MstatusBit::Mpv;
    return mask;
}

/** Project an MPP field value to a strongly typed PrivilegeLevel. */
constexpr auto mstatus_mpp(simrv::xlen::CsrValue mstatus) noexcept -> simrv::xlen::PrivilegeLevel {
    return static_cast<simrv::xlen::PrivilegeLevel>((mstatus & MstatusBit::Mpp) >> 11U);
}

/** Project an SPP field value to a strongly typed PrivilegeLevel. */
constexpr auto mstatus_spp(simrv::xlen::CsrValue mstatus) noexcept -> simrv::xlen::PrivilegeLevel {
    return (mstatus & MstatusBit::Spp) ? simrv::xlen::PrivilegeLevel::Supervisor
                                       : simrv::xlen::PrivilegeLevel::User;
}

/** Embed a strongly typed PrivilegeLevel into the MPP field of an mstatus value. */
constexpr auto mstatus_with_mpp(simrv::xlen::CsrValue mstatus,
                                simrv::xlen::PrivilegeLevel priv) noexcept
    -> simrv::xlen::CsrValue {
    return (mstatus & ~MstatusBit::Mpp) |
           (static_cast<simrv::xlen::CsrValue>(std::to_underlying(priv)) << 11U);
}

/** Embed a strongly typed PrivilegeLevel into the SPP field of an mstatus value. */
constexpr auto mstatus_with_spp(simrv::xlen::CsrValue mstatus,
                                simrv::xlen::PrivilegeLevel priv) noexcept
    -> simrv::xlen::CsrValue {
    return (mstatus & ~MstatusBit::Spp) |
           (static_cast<simrv::xlen::CsrValue>(std::to_underlying(priv)) << 8U);
}

/** MPP value mandated after xRET: U when present, otherwise M. */
constexpr auto mstatus_mpp_after_xret(bool has_u) -> simrv::xlen::PrivilegeLevel {
    return has_u ? simrv::xlen::PrivilegeLevel::User : simrv::xlen::PrivilegeLevel::Machine;
}

/** SRET always clears SPP, encoding User mode. */
constexpr auto mstatus_spp_after_sret() -> simrv::xlen::PrivilegeLevel {
    return simrv::xlen::PrivilegeLevel::User;
}

/** Legalize MPP to a privilege mode implemented by this hart. */
constexpr auto mstatus_legalize_mpp(simrv::xlen::CsrValue value, bool has_s, bool has_u)
    -> simrv::xlen::CsrValue {
    const simrv::xlen::PrivilegeLevel req = mstatus_mpp(value);
    const bool legal = req == simrv::xlen::PrivilegeLevel::Machine ||
                       (req == simrv::xlen::PrivilegeLevel::Supervisor && has_s) ||
                       (req == simrv::xlen::PrivilegeLevel::User && has_u);
    if (legal) return value;
    return mstatus_with_mpp(value, simrv::xlen::PrivilegeLevel::Machine);
}

/** Project an internal status value into the active architectural CSR width. */
constexpr auto mstatus_read_value(simrv::xlen::CsrValue mstatus, simrv::xlen::CsrValue mask,
                                  unsigned xlen) -> simrv::xlen::CsrValue {
    simrv::xlen::CsrValue value = mstatus_with_sd(mstatus) & mask;
    if constexpr (simrv::xlen::kIsXLen64) {
        if (xlen == 32) {
            const bool sd = (value & kMstatusSd) != 0;
            value = static_cast<simrv::xlen::CsrValue>(static_cast<uint32_t>(value));
            if (sd) value |= simrv::xlen::CsrValue{1} << 31U;
        }
    }
    return value;
}

/**
 * @struct MstatusView
 * @brief Strongly typed architectural view for mstatus/sstatus values.
 */
struct MstatusView {
    simrv::xlen::CsrValue value{0};

    constexpr MstatusView() noexcept = default;
    constexpr explicit MstatusView(simrv::xlen::CsrValue val) noexcept : value(val) {}
    constexpr operator simrv::xlen::CsrValue() const noexcept { return value; }

    [[nodiscard]] constexpr auto uie() const noexcept -> bool {
        return (value & MstatusBit::Uie) != 0;
    }
    [[nodiscard]] constexpr auto sie() const noexcept -> bool {
        return (value & MstatusBit::Sie) != 0;
    }
    [[nodiscard]] constexpr auto mie() const noexcept -> bool {
        return (value & MstatusBit::Mie) != 0;
    }
    [[nodiscard]] constexpr auto upie() const noexcept -> bool {
        return (value & MstatusBit::Upie) != 0;
    }
    [[nodiscard]] constexpr auto spie() const noexcept -> bool {
        return (value & MstatusBit::Spie) != 0;
    }
    [[nodiscard]] constexpr auto mpie() const noexcept -> bool {
        return (value & MstatusBit::Mpie) != 0;
    }
    [[nodiscard]] constexpr auto spp() const noexcept -> simrv::xlen::PrivilegeLevel {
        return mstatus_spp(value);
    }
    [[nodiscard]] constexpr auto mpp() const noexcept -> simrv::xlen::PrivilegeLevel {
        return mstatus_mpp(value);
    }
    [[nodiscard]] constexpr auto fs() const noexcept -> uint8_t {
        return static_cast<uint8_t>((value >> 13U) & 0x3U);
    }
    [[nodiscard]] constexpr auto vs() const noexcept -> uint8_t {
        return static_cast<uint8_t>((value >> 9U) & 0x3U);
    }
    [[nodiscard]] constexpr auto mprv() const noexcept -> bool {
        return (value & MstatusBit::Mprv) != 0;
    }
    [[nodiscard]] constexpr auto sum() const noexcept -> bool {
        return (value & MstatusBit::Sum) != 0;
    }
    [[nodiscard]] constexpr auto mxr() const noexcept -> bool {
        return (value & MstatusBit::Mxr) != 0;
    }
    [[nodiscard]] constexpr auto tvm() const noexcept -> bool {
        return (value & MstatusBit::Tvm) != 0;
    }
    [[nodiscard]] constexpr auto tw() const noexcept -> bool {
        return (value & MstatusBit::Tw) != 0;
    }
    [[nodiscard]] constexpr auto tsr() const noexcept -> bool {
        return (value & MstatusBit::Tsr) != 0;
    }
    [[nodiscard]] constexpr auto sd() const noexcept -> bool { return (value & kMstatusSd) != 0; }

    [[nodiscard]] constexpr auto with_mpp(simrv::xlen::PrivilegeLevel priv) const noexcept
        -> MstatusView {
        return MstatusView{mstatus_with_mpp(value, priv)};
    }
    [[nodiscard]] constexpr auto with_spp(simrv::xlen::PrivilegeLevel priv) const noexcept
        -> MstatusView {
        return MstatusView{mstatus_with_spp(value, priv)};
    }
    [[nodiscard]] constexpr auto with_mie(bool enable) const noexcept -> MstatusView {
        return MstatusView{enable ? (value | MstatusBit::Mie) : (value & ~MstatusBit::Mie)};
    }
    [[nodiscard]] constexpr auto with_mpie(bool enable) const noexcept -> MstatusView {
        return MstatusView{enable ? (value | MstatusBit::Mpie) : (value & ~MstatusBit::Mpie)};
    }
    [[nodiscard]] constexpr auto with_sie(bool enable) const noexcept -> MstatusView {
        return MstatusView{enable ? (value | MstatusBit::Sie) : (value & ~MstatusBit::Sie)};
    }
    [[nodiscard]] constexpr auto with_spie(bool enable) const noexcept -> MstatusView {
        return MstatusView{enable ? (value | MstatusBit::Spie) : (value & ~MstatusBit::Spie)};
    }
    [[nodiscard]] constexpr auto with_mprv(bool enable) const noexcept -> MstatusView {
        return MstatusView{enable ? (value | MstatusBit::Mprv) : (value & ~MstatusBit::Mprv)};
    }
};

/**
 * @struct SatpView
 * @brief Strongly typed architectural view for satp address translation register.
 */
struct SatpView {
    simrv::xlen::CsrValue value{0};

    constexpr SatpView() noexcept = default;
    constexpr explicit SatpView(simrv::xlen::CsrValue val) noexcept : value(val) {}
    constexpr operator simrv::xlen::CsrValue() const noexcept { return value; }
    [[nodiscard]] constexpr auto translation_enabled() const noexcept -> bool {
        return mode() != 0;
    }

    [[nodiscard]] constexpr auto mode() const noexcept -> uint8_t {
        if constexpr (simrv::xlen::kIsXLen64) {
            return static_cast<uint8_t>((value >> 60U) & 0xFU);
        } else {
            return static_cast<uint8_t>((value >> 31U) & 0x1U);
        }
    }
    [[nodiscard]] constexpr auto asid() const noexcept -> uint16_t {
        if constexpr (simrv::xlen::kIsXLen64) {
            return static_cast<uint16_t>((value >> 44U) & 0xFFFFU);
        } else {
            return static_cast<uint16_t>((value >> 22U) & 0x1FFU);
        }
    }
    [[nodiscard]] constexpr auto ppn() const noexcept -> uint64_t {
        if constexpr (simrv::xlen::kIsXLen64) {
            return value & 0xFFFFFFFFFFFULL;
        } else {
            return value & 0x3FFFFFULL;
        }
    }
};

/**
 * @struct MisaView
 * @brief Strongly typed architectural view for misa extension register.
 */
struct MisaView {
    simrv::xlen::CsrValue value{0};

    [[nodiscard]] constexpr auto has_extension(char ext) const noexcept -> bool {
        if (ext >= 'a' && ext <= 'z') ext = static_cast<char>(ext - 'a' + 'A');
        if (ext >= 'A' && ext <= 'Z') {
            const auto bit = static_cast<unsigned>(ext - 'A');
            return (value & (simrv::xlen::CsrValue{1} << bit)) != 0;
        }
        return false;
    }
    [[nodiscard]] constexpr auto mxl() const noexcept -> uint8_t {
        return static_cast<uint8_t>(value >> (simrv::xlen::kXLenBits - 2u));
    }
};

/** Return whether a PMP CSR address exists for the active architectural XLEN. */
constexpr auto pmp_csr_exists(simrv::xlen::CsrNumber address, unsigned xlen) -> bool {
    const uint16_t addr = address.value();
    if (addr >= 0x3A0 && addr <= 0x3AF) {
        // RV64 packs eight configuration bytes per even-numbered pmpcfg CSR;
        // the odd-numbered addresses are illegal rather than zero-valued.
        return xlen == 32 || (addr & 1U) == 0;
    }
    return addr >= 0x3B0 && addr <= 0x3EF;
}

/** Legalize one pmpcfg byte: reserved bits read as zero and W requires R. */
constexpr auto pmpcfg_legalize(uint8_t value) -> uint8_t {
    value &= 0x9FU;  // L, A, and R/W/X are the implemented fields.
    if ((value & 0x3U) == 0x2U) value &= static_cast<uint8_t>(~0x2U);
    return value;
}

/** Debug Mode CSR encodings (0x7B0-0x7BF) are unavailable to ordinary U/S/M execution. */
constexpr auto is_debug_csr(simrv::xlen::CsrNumber address) -> bool {
    const uint16_t addr = address.value();
    return addr >= 0x7B0 && addr <= 0x7BF;
}

/** Apply architectural privilege, presence, and read-only checks to a CSR encoding. */
constexpr auto csr_access_permitted(simrv::xlen::PrivilegeLevel current_priv, bool has_s,
                                    bool has_u, simrv::xlen::CsrNumber address, bool is_write)
    -> bool {
    if (is_debug_csr(address)) return false;
    const uint16_t addr = address.value();
    const simrv::xlen::Word required_priv = (addr >> 8U) & 0x3U;
    const bool read_only = address.is_read_only();
    if (required_priv == 1U && !has_s) return false;
    if (!has_s && (addr == 0x302U || addr == 0x303U)) return false;
    if (!has_u && addr == 0x306U) return false;
    if (static_cast<simrv::xlen::Word>(current_priv) < required_priv) return false;
    return !is_write || !read_only;
}

/**
 * @brief Identify implemented hardwired-zero HPM CSRs for the active XLEN.
 *
 * RV32 defines high halves of its 64-bit counters. The same addresses are
 * reserved in RV64 and must not appear as implemented zero-valued CSRs.
 */
constexpr auto is_zero_hpm_csr(simrv::xlen::CsrNumber address, unsigned xlen) -> bool {
    const uint16_t addr = address.value();
    if ((addr >= 0x323U && addr <= 0x33FU) || (addr >= 0xB03U && addr <= 0xB1FU) ||
        (addr >= 0xC03U && addr <= 0xC1FU)) {
        return true;
    }
    return xlen == 32U &&
           ((addr >= 0xB83U && addr <= 0xB9FU) || (addr >= 0xC83U && addr <= 0xC9FU));
}

/**
 * @brief Convert an internal cause value to the active architectural XLEN.
 *
 * RV64 builds can execute an RV32 personality while retaining bit 63 as the
 * internal interrupt marker. Native RV32 builds already use bit 31 and must
 * not attempt that translation.
 */
constexpr auto cause_read_value(simrv::xlen::CsrValue value, unsigned xlen)
    -> simrv::xlen::CsrValue {
    if constexpr (simrv::xlen::kIsXLen64) {
        constexpr auto kInternalInterrupt = static_cast<simrv::xlen::CsrValue>(uint64_t{1} << 63U);
        if (xlen == 32) {
            const bool interrupt = (value & kInternalInterrupt) != 0;
            value = static_cast<simrv::xlen::CsrValue>(static_cast<uint32_t>(value));
            if (interrupt) value |= simrv::xlen::CsrValue{1} << 31U;
        }
    }
    return value;
}

/** Convert a software-written architectural cause to the internal representation. */
constexpr auto cause_write_value(simrv::xlen::CsrValue value, unsigned xlen)
    -> simrv::xlen::CsrValue {
    if constexpr (simrv::xlen::kIsXLen64) {
        constexpr simrv::xlen::CsrValue kArchitecturalRv32Interrupt = simrv::xlen::CsrValue{1}
                                                                      << 31U;
        constexpr auto kInternalInterrupt = static_cast<simrv::xlen::CsrValue>(uint64_t{1} << 63U);
        if (xlen == 32) {
            value = static_cast<simrv::xlen::CsrValue>(static_cast<uint32_t>(value));
            if ((value & kArchitecturalRv32Interrupt) != 0) {
                return (value & ~kArchitecturalRv32Interrupt) | kInternalInterrupt;
            }
        }
    }
    return value;
}

enum class Csr : uint16_t {
    // Uepc remains named because URET decoding uses its address before the
    // instruction is rejected when the unsupported N extension is disabled.
    Uepc = 0x041,
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
    Sie = 0x104,
    Stvec = 0x105,
    Scounteren = 0x106,
    Senvcfg = 0x10A,
    Sstateen0 = 0x10C,
    Sstateen1 = 0x10D,
    Sstateen2 = 0x10E,
    Sstateen3 = 0x10F,
    Vsstatus = 0x200,
    Vsie = 0x204,
    Vstvec = 0x205,
    Vsscratch = 0x240,
    Vsepc = 0x241,
    Vscause = 0x242,
    Vstval = 0x243,
    Vsip = 0x244,
    Vsatp = 0x280,
    Sscratch = 0x140,
    Sepc = 0x141,
    Scause = 0x142,
    Stval = 0x143,
    Sip = 0x144,
    Stimecmp = 0x14D,
    Stimecmph = 0x15D,
    Scountovf = 0xDA0,
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
    Menvcfg = 0x30A,
    Mstateen0 = 0x30C,
    Mstateen1 = 0x30D,
    Mstateen2 = 0x30E,
    Mstateen3 = 0x30F,
    Mcountinhibit = 0x320,
    Menvcfgh = 0x31A,
    Mstateen0h = 0x31C,
    Mstateen1h = 0x31D,
    Mstateen2h = 0x31E,
    Mstateen3h = 0x31F,
    Hstateen0 = 0x60C,
    Hstateen1 = 0x60D,
    Hstateen2 = 0x60E,
    Hstateen3 = 0x60F,
    Hstatus = 0x600,
    Hedeleg = 0x602,
    Hideleg = 0x603,
    Hie = 0x604,
    Hcounteren = 0x606,
    Henvcfg = 0x60A,
    Htval = 0x643,
    Hip = 0x644,
    Hvip = 0x645,
    Htinst = 0x64A,
    Hgatp = 0x680,
    Mscratch = 0x340,
    Mepc = 0x341,
    Mcause = 0x342,
    Mtval = 0x343,
    Mip = 0x344,
    Mseccfg = 0x747,
    Mseccfgh = 0x757,
    Tselect = 0x7A0,
    Tdata1 = 0x7A1,
    Tdata2 = 0x7A2,
    Tdata3 = 0x7A3,
    Tinfo = 0x7A4,
    Tcontrol = 0x7A5,
    Mcontext = 0x7A8,
    Scontext = 0x7AA,
    Mcycle = 0xB00,
    Minstret = 0xB02,
    Mcycleh = 0xB80,
    Minstreth = 0xB82,
    Cycleh = 0xC80,
    Timeh = 0xC81,
    Instreth = 0xC82,
};

constexpr uint16_t csr_addr(Csr csr) noexcept { return static_cast<uint16_t>(csr); }
constexpr simrv::xlen::CsrNumber csr_num(Csr csr) noexcept { return simrv::xlen::CsrNumber{csr}; }

namespace csr {
inline constexpr simrv::xlen::CsrNumber kMstatus{0x300};
inline constexpr simrv::xlen::CsrNumber kMisa{0x301};
inline constexpr simrv::xlen::CsrNumber kMedeleg{0x302};
inline constexpr simrv::xlen::CsrNumber kMideleg{0x303};
inline constexpr simrv::xlen::CsrNumber kMie{0x304};
inline constexpr simrv::xlen::CsrNumber kMtvec{0x305};
inline constexpr simrv::xlen::CsrNumber kMcounteren{0x306};
inline constexpr simrv::xlen::CsrNumber kMstatush{0x310};
inline constexpr simrv::xlen::CsrNumber kMcountinhibit{0x320};
inline constexpr simrv::xlen::CsrNumber kMscratch{0x340};
inline constexpr simrv::xlen::CsrNumber kMepc{0x341};
inline constexpr simrv::xlen::CsrNumber kMcause{0x342};
inline constexpr simrv::xlen::CsrNumber kMtval{0x343};
inline constexpr simrv::xlen::CsrNumber kMip{0x344};

inline constexpr simrv::xlen::CsrNumber kSstatus{0x100};
inline constexpr simrv::xlen::CsrNumber kSie{0x104};
inline constexpr simrv::xlen::CsrNumber kStvec{0x105};
inline constexpr simrv::xlen::CsrNumber kScounteren{0x106};
inline constexpr simrv::xlen::CsrNumber kSscratch{0x140};
inline constexpr simrv::xlen::CsrNumber kSepc{0x141};
inline constexpr simrv::xlen::CsrNumber kScause{0x142};
inline constexpr simrv::xlen::CsrNumber kStval{0x143};
inline constexpr simrv::xlen::CsrNumber kSip{0x144};
inline constexpr simrv::xlen::CsrNumber kSatp{0x180};

inline constexpr simrv::xlen::CsrNumber kFflags{0x001};
inline constexpr simrv::xlen::CsrNumber kFrm{0x002};
inline constexpr simrv::xlen::CsrNumber kFcsr{0x003};

inline constexpr simrv::xlen::CsrNumber kTselect{0x7A0};
inline constexpr simrv::xlen::CsrNumber kTdata1{0x7A1};
inline constexpr simrv::xlen::CsrNumber kTdata2{0x7A2};
inline constexpr simrv::xlen::CsrNumber kTdata3{0x7A3};
inline constexpr simrv::xlen::CsrNumber kTinfo{0x7A4};
inline constexpr simrv::xlen::CsrNumber kTcontrol{0x7A5};
inline constexpr simrv::xlen::CsrNumber kMcontext{0x7A8};
inline constexpr simrv::xlen::CsrNumber kScontext{0x7AA};

inline constexpr simrv::xlen::CsrNumber kCycle{0xC00};
inline constexpr simrv::xlen::CsrNumber kTime{0xC01};
inline constexpr simrv::xlen::CsrNumber kInstret{0xC02};
}  // namespace csr

}  // namespace simrv::core
