#pragma once

#include "simrv/Define.hpp"
#include "simrv/memory/MemoryAccess.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::xlen {

// Fetch size in bytes based on XLEN
inline constexpr unsigned kFetchSize = (kIsXLen64 ? 8u : 4u);

// Address mask respecting XLEN width
inline constexpr Word kAddrMask = (kIsXLen64 ? UINT64_MAX : UINT32_MAX);
inline constexpr Address maskAddress(Address a) noexcept { return a & kAddrMask; }

inline auto resolve_misa_string(CSRValue misa) -> std::string {
    std::string s = kIsXLen64 ? "RV64" : "RV32";
    bool has_i = (misa & (static_cast<CSRValue>(1) << ('i' - 'a'))) != 0;
    bool has_m = (misa & (static_cast<CSRValue>(1) << ('m' - 'a'))) != 0;
    bool has_a = (misa & (static_cast<CSRValue>(1) << ('a' - 'a'))) != 0;
    bool has_f = (misa & (static_cast<CSRValue>(1) << ('f' - 'a'))) != 0;
    bool has_d = (misa & (static_cast<CSRValue>(1) << ('d' - 'a'))) != 0;
    bool is_g = has_i && has_m && has_a && has_f && has_d;

    if (is_g) {
        s += "G";
    }

    for (int i = 0; i < 26; ++i) {
        if ((misa & (static_cast<CSRValue>(1) << i)) != 0) {
            char c = static_cast<char>('a' + i);
            if (is_g && (c == 'i' || c == 'm' || c == 'a' || c == 'f' || c == 'd')) {
                continue;
            }
            s += static_cast<char>(c - 'a' + 'A');
        }
    }
    return s;
}

inline auto resolve_cause_string(uint64_t cause) -> std::string {
    if (cause == 0) return "None";

    const bool is_interrupt = (cause & (1ULL << (kXLenBits - 1))) != 0;
    const uint64_t code = cause & ~(1ULL << (kXLenBits - 1));

    if (is_interrupt) {
        switch (code) {
            case 1:
                return "S-SoftwareInt";
            case 3:
                return "M-SoftwareInt";
            case 5:
                return "S-TimerInt";
            case 7:
                return "M-TimerInt";
            case 9:
                return "S-ExternalInt";
            case 11:
                return "M-ExternalInt";
            default:
                return std::format("Interrupt #{}", code);
        }
    } else {
        switch (code) {
            case 0:
                return "AddrMisalignedFetch";
            case 1:
                return "AccessFaultFetch";
            case 2:
                return "IllegalInst";
            case 3:
                return "Breakpoint";
            case 4:
                return "AddrMisalignedLoad";
            case 5:
                return "AccessFaultLoad";
            case 6:
                return "AddrMisalignedStore";
            case 7:
                return "AccessFaultStore";
            case 8:
                return "U-Ecall";
            case 9:
                return "S-Ecall";
            case 11:
                return "M-Ecall";
            case 12:
                return "PageFaultFetch";
            case 13:
                return "PageFaultLoad";
            case 15:
                return "PageFaultStore";
            default:
                return std::format("Exception #{}", code);
        }
    }
}

inline auto resolve_mstatus_short_string(CSRValue mstatus) -> std::string {
    std::string s;
    CSRValue mpp = (mstatus & enum_mask(MstatusBit::Mpp)) >> 11;
    s += (mpp == 3 ? "M" : mpp == 1 ? "S" : "U");

    if ((mstatus & enum_mask(MstatusBit::Mie)) != 0) s += "|MIE";
    if ((mstatus & enum_mask(MstatusBit::Sie)) != 0) s += "|SIE";
    if ((mstatus & enum_mask(MstatusBit::Sum)) != 0) s += "|SUM";
    if ((mstatus & enum_mask(MstatusBit::Mprv)) != 0) s += "|MPRV";
    if ((mstatus & enum_mask(MstatusBit::Mxr)) != 0) s += "|MXR";
    if ((mstatus & enum_mask(MstatusBit::Tvm)) != 0) s += "|TVM";
    if ((mstatus & enum_mask(MstatusBit::Tsr)) != 0) s += "|TSR";

    return std::format("[{}]", s);
}

inline auto resolve_satp_string(CSRValue satp, unsigned xlen = kXLenBits) -> std::string {
    Word mode = satp_mode(satp, xlen);
    if (mode == 0) return "BARE";
    if (xlen == 64) {
        if (mode == 1) return "SV32";
        if (mode == 8) return "SV39";
        if (mode == 9) return "SV48";
    } else {
        if (mode == 1) return "SV32";
    }
    return std::format("MODE{}", mode);
}

}  // namespace simrv::xlen
