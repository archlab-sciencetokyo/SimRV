/**
 * @file Mmio.hpp
 * @brief Shared MMIO constants and address helpers.
 */
#pragma once

#include <algorithm>
#include <cstdint>

#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::mmio {

// Historical interrupt-controller address map used by the standalone devices.
inline constexpr simrv::xlen::Address kPlicBaseAddress =
    static_cast<simrv::xlen::Address>(0x50000000u);
inline constexpr simrv::xlen::Address kPlicSize = static_cast<simrv::xlen::Address>(0x04000000u);
inline constexpr simrv::xlen::Address kClintBaseAddress =
    static_cast<simrv::xlen::Address>(0x60000000u);
inline constexpr simrv::xlen::Address kClintSize = static_cast<simrv::xlen::Address>(0x000c0000u);

// RISC-V AIA (APLIC / IMSIC) and ACLINT addresses
inline constexpr simrv::xlen::Address kAclintMtimerBaseAddress =
    static_cast<simrv::xlen::Address>(0x02000000u);
inline constexpr simrv::xlen::Address kAclintMtimerSize =
    static_cast<simrv::xlen::Address>(0x00010000u);
inline constexpr simrv::xlen::Address kAclintMswiBaseAddress =
    static_cast<simrv::xlen::Address>(0x02010000u);
inline constexpr simrv::xlen::Address kAclintMswiSize =
    static_cast<simrv::xlen::Address>(0x00010000u);

inline constexpr simrv::xlen::Address kAplicMBaseAddress =
    static_cast<simrv::xlen::Address>(0x0c000000u);
inline constexpr simrv::xlen::Address kAplicMSize = static_cast<simrv::xlen::Address>(0x00004000u);
inline constexpr simrv::xlen::Address kAplicSBaseAddress =
    static_cast<simrv::xlen::Address>(0x0d000000u);
inline constexpr simrv::xlen::Address kAplicSSize = static_cast<simrv::xlen::Address>(0x00004000u);

inline constexpr simrv::xlen::Address kImsicMBaseAddress =
    static_cast<simrv::xlen::Address>(0x24000000u);
inline constexpr simrv::xlen::Address kImsicMSize = static_cast<simrv::xlen::Address>(0x00010000u);
inline constexpr simrv::xlen::Address kImsicSBaseAddress =
    static_cast<simrv::xlen::Address>(0x28000000u);
inline constexpr simrv::xlen::Address kImsicSSize = static_cast<simrv::xlen::Address>(0x00010000u);

// PCIe ECAM hierarchy and MMIO 32-bit window
inline constexpr simrv::xlen::Address kPcieEcamBaseAddress =
    static_cast<simrv::xlen::Address>(0x30000000u);
inline constexpr simrv::xlen::Address kPcieEcamSize =
    static_cast<simrv::xlen::Address>(0x10000000u);
inline constexpr simrv::xlen::Address kPcieMmioBaseAddress =
    static_cast<simrv::xlen::Address>(0x40000000u);
inline constexpr simrv::xlen::Address kPcieMmioSize =
    static_cast<simrv::xlen::Address>(0x10000000u);

// Standard VirtIO-MMIO v2 base address range (0x10001000..0x10008000)
inline constexpr simrv::xlen::Address kVirtioMmioBaseAddress =
    static_cast<simrv::xlen::Address>(0x10001000u);
inline constexpr simrv::xlen::Address kVirtioMmioSlotSize =
    static_cast<simrv::xlen::Address>(0x1000u);

// Standalone DMA Controller (0x10009000..0x1000A000)
inline constexpr simrv::xlen::Address kDmaControllerBaseAddress =
    static_cast<simrv::xlen::Address>(0x10009000u);
inline constexpr simrv::xlen::Address kDmaControllerSize =
    static_cast<simrv::xlen::Address>(0x00001000u);
inline constexpr uint32_t kDmaControllerIrq = 12;

inline constexpr simrv::xlen::Address kUartBaseAddress =
    static_cast<simrv::xlen::Address>(0x10000000u);
inline constexpr simrv::xlen::Address kUartSize = static_cast<simrv::xlen::Address>(0x00000100u);

inline constexpr simrv::xlen::Word kUartLcrDlabMask = static_cast<simrv::xlen::Word>(0x80U);
inline constexpr simrv::xlen::Word kUartLsrDataReady = static_cast<simrv::xlen::Word>(0x01U);
inline constexpr simrv::xlen::Word kUartLsrThreTemt = static_cast<simrv::xlen::Word>(0x60U);

inline constexpr simrv::xlen::Address kUartRegRbrThrDll = static_cast<simrv::xlen::Address>(0x00U);
inline constexpr simrv::xlen::Address kUartRegIerDlm = static_cast<simrv::xlen::Address>(0x01U);
inline constexpr simrv::xlen::Address kUartRegIirFcr = static_cast<simrv::xlen::Address>(0x02U);
inline constexpr simrv::xlen::Address kUartRegLcr = static_cast<simrv::xlen::Address>(0x03U);
inline constexpr simrv::xlen::Address kUartRegMcr = static_cast<simrv::xlen::Address>(0x04U);
inline constexpr simrv::xlen::Address kUartRegLsr = static_cast<simrv::xlen::Address>(0x05U);
inline constexpr simrv::xlen::Address kUartRegMsr = static_cast<simrv::xlen::Address>(0x06U);
inline constexpr simrv::xlen::Address kUartRegScr = static_cast<simrv::xlen::Address>(0x07U);
inline constexpr simrv::xlen::Address kUartRegInvalid =
    static_cast<simrv::xlen::Address>(~simrv::xlen::Address{0});

inline constexpr auto contains(simrv::xlen::Address addr, simrv::xlen::Address base,
                               simrv::xlen::Address size) -> bool {
    return addr >= base && addr < (base + size);
}

inline constexpr auto uart_reg_byte(simrv::xlen::Address raw) -> simrv::xlen::Address {
    switch (raw & static_cast<simrv::xlen::Address>(0x07U)) {
        case kUartRegRbrThrDll:
            return kUartRegRbrThrDll;
        case kUartRegIerDlm:
            return kUartRegIerDlm;
        case kUartRegIirFcr:
            return kUartRegIirFcr;
        case kUartRegLcr:
            return kUartRegLcr;
        case kUartRegMcr:
            return kUartRegMcr;
        case kUartRegLsr:
            return kUartRegLsr;
        case kUartRegMsr:
            return kUartRegMsr;
        case kUartRegScr:
            return kUartRegScr;
        default:
            return kUartRegInvalid;
    }
}

inline constexpr auto uart_reg_word(simrv::xlen::Address raw) -> simrv::xlen::Address {
    switch (raw) {
        case static_cast<simrv::xlen::Address>(0x00U):
            return kUartRegRbrThrDll;
        case static_cast<simrv::xlen::Address>(0x04U):
            return kUartRegIerDlm;
        case static_cast<simrv::xlen::Address>(0x08U):
            return kUartRegIirFcr;
        case static_cast<simrv::xlen::Address>(0x0CU):
            return kUartRegLcr;
        case static_cast<simrv::xlen::Address>(0x10U):
            return kUartRegMcr;
        case static_cast<simrv::xlen::Address>(0x14U):
            return kUartRegLsr;
        case static_cast<simrv::xlen::Address>(0x18U):
            return kUartRegMsr;
        case static_cast<simrv::xlen::Address>(0x1CU):
            return kUartRegScr;
        default:
            return kUartRegInvalid;
    }
}

inline auto uart_reg(simrv::xlen::Address p_addr, int8_t& reg_shift_mode) -> simrv::xlen::Address {
    const simrv::xlen::Address raw = p_addr - kUartBaseAddress;
    if (reg_shift_mode < 0) {
        if (raw == static_cast<simrv::xlen::Address>(0x01U) ||
            raw == static_cast<simrv::xlen::Address>(0x02U) ||
            raw == static_cast<simrv::xlen::Address>(0x03U) ||
            raw == static_cast<simrv::xlen::Address>(0x05U) ||
            raw == static_cast<simrv::xlen::Address>(0x06U) ||
            raw == static_cast<simrv::xlen::Address>(0x07U)) {
            reg_shift_mode = 0;
        }

        if (raw == static_cast<simrv::xlen::Address>(0x08U) ||
            raw == static_cast<simrv::xlen::Address>(0x0CU) ||
            raw == static_cast<simrv::xlen::Address>(0x10U) ||
            raw == static_cast<simrv::xlen::Address>(0x14U) ||
            raw == static_cast<simrv::xlen::Address>(0x18U) ||
            raw == static_cast<simrv::xlen::Address>(0x1CU)) {
            reg_shift_mode = 2;
        }

        reg_shift_mode = std::max<int8_t>(reg_shift_mode, 0);
    }

    if (reg_shift_mode == 2) {
        return uart_reg_word(raw);
    }

    return uart_reg_byte(raw);
}

}  // namespace simrv::mmio
