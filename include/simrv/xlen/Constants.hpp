/**
 * @file Constants.hpp
 * @brief Architectural constants and bit-masks.
 */
#pragma once

#include "simrv/xlen/Types.hpp"

namespace simrv::xlen {

inline constexpr Word kWordAllOnes = ~Word(0);

[[nodiscard]] inline constexpr auto xlen_shift_mask() -> Word {
    return static_cast<Word>(kXLenBits - 1u);
}

[[nodiscard]] consteval auto make_mask(unsigned bits) -> Word {
    if (bits >= kXLenBits) {
        return kWordAllOnes;
    }
    return (static_cast<Word>(1) << bits) - static_cast<Word>(1);
}

inline constexpr Word kXLenMask = make_mask(kXLenBits);
inline constexpr Word kLower32Mask = make_mask(32);

// Boxed single-precision float marker in the floating register space.
// Represented as upper 32 bits set (0xffffffff << 32) in the 64-bit floating container.
inline constexpr FloatingRegister kF32BoxerBits =
    static_cast<FloatingRegister>(static_cast<FloatingRegister>(kLower32Mask) << 32);

// =========================================================================
// IEEE 754 Floating-Point Constants
// =========================================================================
inline constexpr uint32_t kF32SignBit = 31U;
inline constexpr uint32_t kF32ExpMask = 0xffU;
inline constexpr uint32_t kF32ExpShift = 23U;
inline constexpr uint32_t kF32FracMask = 0x7fffffU;
inline constexpr uint32_t kF32Qnan = 0x7fc00000U;
inline constexpr uint32_t kF32FracQnanBit = 22U;

inline constexpr uint64_t kF64SignBit = 63U;
inline constexpr uint64_t kF64ExpMask = 0x7ffU;
inline constexpr uint64_t kF64ExpShift = 52U;
inline constexpr uint64_t kF64FracMask = 0xfffffffffffffULL;
inline constexpr uint64_t kF64Qnan = 0x7ff8000000000000ULL;
inline constexpr uint64_t kF64FracQnanBit = 51U;

// =========================================================================
// SATP CSR Layouts (Strict RISC-V Standard)
// =========================================================================
inline constexpr Word kMegapageOffsetMask = 0x003fffffu;
inline constexpr Word kPageOffsetMask = 0x00000fffu;

template <unsigned XLen = kXLenBits>
[[nodiscard]] inline constexpr auto satp_mode(Word satp) -> Word {
    if constexpr (XLen == 64) {
        return static_cast<Word>((static_cast<uint64_t>(satp) >> 60) & 0xFu);
    } else {
        return (satp >> 31) & 0x1u;
    }
}

template <unsigned XLen = kXLenBits>
[[nodiscard]] inline constexpr auto satp_translation_enabled(Word satp) -> bool {
    return satp_mode<XLen>(satp) != 0;
}

template <unsigned XLen = kXLenBits>
[[nodiscard]] inline constexpr auto satp_mode_supported(Word mode) -> bool {
    if constexpr (XLen == 64) {
        // RV64 MODE=1 is reserved; Sv32 is an RV32 translation scheme.
        return mode == 0 || mode == 8;  // Bare, Sv39
    } else {
        return mode == 0 || mode == 1;  // Bare, SV32
    }
}

template <unsigned XLen = kXLenBits>
[[nodiscard]] inline constexpr auto satp_asid(Word satp) -> Word {
    if constexpr (XLen == 64) {
        return static_cast<Word>((static_cast<uint64_t>(satp) >> 44) & 0xffffu);
    } else {
        return (satp >> 22) & 0x1ffu;
    }
}

template <unsigned XLen = kXLenBits>
[[nodiscard]] inline constexpr auto satp_root_ppn(Word satp) -> Word {
    if constexpr (XLen == 64) {
        // Sv39 and Sv48 use the common RV64 satp PPN field.
        return static_cast<Word>(static_cast<uint64_t>(satp) & 0x00000fffffffffffULL);
    } else {
        return satp & 0x003fffffu;
    }
}

[[nodiscard]] inline auto satp_mode(Word satp, unsigned xlen) -> Word {
    if constexpr (kXLenBits == 32) {
        return satp_mode<32>(satp);
    } else {
        return (xlen == 64) ? satp_mode<64>(satp) : satp_mode<32>(satp);
    }
}

[[nodiscard]] inline auto satp_translation_enabled(Word satp, unsigned xlen) -> bool {
    if constexpr (kXLenBits == 32) {
        return satp_translation_enabled<32>(satp);
    } else {
        return (xlen == 64) ? satp_translation_enabled<64>(satp)
                            : satp_translation_enabled<32>(satp);
    }
}

[[nodiscard]] inline auto satp_mode_supported(Word mode, unsigned xlen) -> bool {
    if constexpr (kXLenBits == 32) {
        return satp_mode_supported<32>(mode);
    } else {
        return (xlen == 64) ? satp_mode_supported<64>(mode) : satp_mode_supported<32>(mode);
    }
}

[[nodiscard]] inline auto satp_asid(Word satp, unsigned xlen) -> Word {
    if constexpr (kXLenBits == 32) {
        return satp_asid<32>(satp);
    } else {
        return (xlen == 64) ? satp_asid<64>(satp) : satp_asid<32>(satp);
    }
}

[[nodiscard]] inline auto satp_root_ppn(Word satp, unsigned xlen) -> Word {
    if constexpr (kXLenBits == 32) {
        return satp_root_ppn<32>(satp);
    } else {
        return (xlen == 64) ? satp_root_ppn<64>(satp) : satp_root_ppn<32>(satp);
    }
}

}  // namespace simrv::xlen

using simrv::xlen::kF32BoxerBits;
using simrv::xlen::kLower32Mask;
using simrv::xlen::kWordAllOnes;
using simrv::xlen::kXLenMask;
using simrv::xlen::make_mask;
using simrv::xlen::xlen_shift_mask;
