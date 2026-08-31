/**
 * @file Mmio.hpp
 * @brief Shared MMIO constants and address helpers.
 */
#pragma once

#include <algorithm>
#include <cstdint>

#include "simrv/xlen/Types.hpp"

namespace simrv::mmio {

// Real-time clock address
inline constexpr Address kRtcBaseAddress = static_cast<Address>(0x70000000u);
inline constexpr Address kRtcSize = static_cast<Address>(0x00001000u);

// Interrupt controller addresses (PLIC / CLINT legacy)
inline constexpr Address kPlicBaseAddress = static_cast<Address>(0x50000000u);
inline constexpr Address kPlicSize = static_cast<Address>(0x04000000u);
inline constexpr Address kPlicHartBase = static_cast<Address>(0x00200000u);
inline constexpr Address kPlicHartSize = static_cast<Address>(0x00001000u);
inline constexpr Address kClintBaseAddress = static_cast<Address>(0x60000000u);
inline constexpr Address kClintSize = static_cast<Address>(0x000c0000u);

// RISC-V AIA (APLIC / IMSIC) and ACLINT addresses
inline constexpr Address kAclintMtimerBaseAddress = static_cast<Address>(0x02000000u);
inline constexpr Address kAclintMtimerSize = static_cast<Address>(0x00010000u);
inline constexpr Address kAclintMswiBaseAddress = static_cast<Address>(0x02010000u);
inline constexpr Address kAclintMswiSize = static_cast<Address>(0x00010000u);

inline constexpr Address kAplicMBaseAddress = static_cast<Address>(0x0c000000u);
inline constexpr Address kAplicMSize = static_cast<Address>(0x00004000u);
inline constexpr Address kAplicSBaseAddress = static_cast<Address>(0x0d000000u);
inline constexpr Address kAplicSSize = static_cast<Address>(0x00004000u);

inline constexpr Address kImsicMBaseAddress = static_cast<Address>(0x24000000u);
inline constexpr Address kImsicMSize = static_cast<Address>(0x00010000u);
inline constexpr Address kImsicSBaseAddress = static_cast<Address>(0x28000000u);
inline constexpr Address kImsicSSize = static_cast<Address>(0x00010000u);

// PCIe ECAM hierarchy and MMIO 32-bit window
inline constexpr Address kPcieEcamBaseAddress = static_cast<Address>(0x30000000u);
inline constexpr Address kPcieEcamSize = static_cast<Address>(0x10000000u);
inline constexpr Address kPcieMmioBaseAddress = static_cast<Address>(0x40000000u);
inline constexpr Address kPcieMmioSize = static_cast<Address>(0x10000000u);

// Standard VirtIO-MMIO v2 base address range (0x10001000..0x10008000)
inline constexpr Address kVirtioMmioBaseAddress = static_cast<Address>(0x10001000u);
inline constexpr Address kVirtioMmioSlotSize = static_cast<Address>(0x1000u);

inline constexpr Address kTohostAddress = static_cast<Address>(0x40008000u);
inline constexpr Address kUartBaseAddress = static_cast<Address>(0x10000000u);
inline constexpr Address kUartSize = static_cast<Address>(0x00000100u);

inline constexpr Word kUartLcrDlabMask = static_cast<Word>(0x80U);
inline constexpr Word kUartLsrDataReady = static_cast<Word>(0x01U);
inline constexpr Word kUartLsrThreTemt = static_cast<Word>(0x60U);

inline constexpr Address kUartRegRbrThrDll = static_cast<Address>(0x00U);
inline constexpr Address kUartRegIerDlm = static_cast<Address>(0x01U);
inline constexpr Address kUartRegIirFcr = static_cast<Address>(0x02U);
inline constexpr Address kUartRegLcr = static_cast<Address>(0x03U);
inline constexpr Address kUartRegMcr = static_cast<Address>(0x04U);
inline constexpr Address kUartRegLsr = static_cast<Address>(0x05U);
inline constexpr Address kUartRegMsr = static_cast<Address>(0x06U);
inline constexpr Address kUartRegScr = static_cast<Address>(0x07U);
inline constexpr Address kUartRegInvalid = static_cast<Address>(~Address{0});

inline constexpr auto contains(Address addr, Address base, Address size) -> bool {
    return addr >= base && addr < (base + size);
}

inline constexpr auto uart_reg_byte(Address raw) -> Address {
    switch (raw & static_cast<Address>(0x07U)) {
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

inline constexpr auto uart_reg_word(Address raw) -> Address {
    switch (raw) {
        case static_cast<Address>(0x00U):
            return kUartRegRbrThrDll;
        case static_cast<Address>(0x04U):
            return kUartRegIerDlm;
        case static_cast<Address>(0x08U):
            return kUartRegIirFcr;
        case static_cast<Address>(0x0CU):
            return kUartRegLcr;
        case static_cast<Address>(0x10U):
            return kUartRegMcr;
        case static_cast<Address>(0x14U):
            return kUartRegLsr;
        case static_cast<Address>(0x18U):
            return kUartRegMsr;
        case static_cast<Address>(0x1CU):
            return kUartRegScr;
        default:
            return kUartRegInvalid;
    }
}

inline auto uart_reg(Address p_addr, int8_t& reg_shift_mode) -> Address {
    const Address raw = p_addr - kUartBaseAddress;
    if (reg_shift_mode < 0) {
        if (raw == static_cast<Address>(0x01U) || raw == static_cast<Address>(0x02U) ||
            raw == static_cast<Address>(0x03U) || raw == static_cast<Address>(0x05U) ||
            raw == static_cast<Address>(0x06U) || raw == static_cast<Address>(0x07U)) {
            reg_shift_mode = 0;
        }

        if (raw == static_cast<Address>(0x08U) || raw == static_cast<Address>(0x0CU) ||
            raw == static_cast<Address>(0x10U) || raw == static_cast<Address>(0x14U) ||
            raw == static_cast<Address>(0x18U) || raw == static_cast<Address>(0x1CU)) {
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