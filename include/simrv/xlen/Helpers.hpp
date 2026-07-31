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
    uint64_t misa64 = static_cast<uint64_t>(misa);
    uint8_t mxl64 = static_cast<uint8_t>(misa64 >> 62);
    uint8_t mxl32 = static_cast<uint8_t>((misa64 >> 30) & 3);
    bool is_rv32 = (mxl32 == 1 && mxl64 == 0);
    bool is_rv64 = (mxl64 == 2);
    std::string s = is_rv32 ? "RV32" : (is_rv64 ? "RV64" : (kIsXLen64 ? "RV64" : "RV32"));

    bool has_i = (misa & (static_cast<CSRValue>(1) << ('i' - 'a'))) != 0;
    bool has_m = (misa & (static_cast<CSRValue>(1) << ('m' - 'a'))) != 0;
    bool has_a = (misa & (static_cast<CSRValue>(1) << ('a' - 'a'))) != 0;
    bool has_f = (misa & (static_cast<CSRValue>(1) << ('f' - 'a'))) != 0;
    bool has_d = (misa & (static_cast<CSRValue>(1) << ('d' - 'a'))) != 0;
    bool has_c = (misa & (static_cast<CSRValue>(1) << ('c' - 'a'))) != 0;
    bool has_b = (misa & (static_cast<CSRValue>(1) << ('b' - 'a'))) != 0;
    bool has_v = (misa & (static_cast<CSRValue>(1) << ('v' - 'a'))) != 0;
    bool has_s = (misa & (static_cast<CSRValue>(1) << ('s' - 'a'))) != 0;
    bool has_u = (misa & (static_cast<CSRValue>(1) << ('u' - 'a'))) != 0;

    bool is_g = has_i && has_m && has_a && has_f && has_d;

    if (is_g) {
        s += "G";
    } else {
        if (has_i) s += "I";
        if (has_m) s += "M";
        if (has_a) s += "A";
        if (has_f) s += "F";
        if (has_d) s += "D";
    }

    if (has_c) s += "C";
    if (has_b) s += "B";
    if (has_v) s += "V";
    if (has_s) s += "S";
    if (has_u) s += "U";

    for (int i = 0; i < 26; ++i) {
        char ch = static_cast<char>('a' + i);
        if (ch == 'i' || ch == 'm' || ch == 'a' || ch == 'f' || ch == 'd' || ch == 'c' ||
            ch == 'b' || ch == 'v' || ch == 's' || ch == 'u') {
            continue;
        }
        if ((misa & (static_cast<CSRValue>(1) << i)) != 0) {
            s += static_cast<char>(ch - 'a' + 'A');
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
    CSRValue mpp = (mstatus & enum_mask(core::MstatusBit::Mpp)) >> 11;
    s += (mpp == 3 ? "M" : mpp == 1 ? "S" : "U");

    if ((mstatus & enum_mask(core::MstatusBit::Mie)) != 0) s += "|MIE";
    if ((mstatus & enum_mask(core::MstatusBit::Sie)) != 0) s += "|SIE";
    if ((mstatus & enum_mask(core::MstatusBit::Sum)) != 0) s += "|SUM";
    if ((mstatus & enum_mask(core::MstatusBit::Mprv)) != 0) s += "|MPRV";
    if ((mstatus & enum_mask(core::MstatusBit::Mxr)) != 0) s += "|MXR";
    if ((mstatus & enum_mask(core::MstatusBit::Tvm)) != 0) s += "|TVM";
    if ((mstatus & enum_mask(core::MstatusBit::Tsr)) != 0) s += "|TSR";

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
