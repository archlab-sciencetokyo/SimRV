/**
 * @file Mmio.hpp
 * @brief Shared MMIO constants and address helpers.
 */
#pragma once

#include <algorithm>
#include <cstdint>

#include "simrv/core/PlatformMap.h"
#include "simrv/xlen/AddressTypes.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::mmio {

// Historical interrupt-controller address map used by the standalone devices.
inline constexpr simrv::xlen::PhysAddr kPlicBaseAddress{0x50000000u};
inline constexpr simrv::xlen::Address kPlicSize = static_cast<simrv::xlen::Address>(0x04000000u);
inline constexpr simrv::xlen::PhysAddr kClintBaseAddress{0x60000000u};
inline constexpr simrv::xlen::Address kClintSize = static_cast<simrv::xlen::Address>(0x000c0000u);

// RISC-V AIA (APLIC / IMSIC) and ACLINT addresses
inline constexpr simrv::xlen::PhysAddr kAclintMtimerBaseAddress{0x02000000u};
inline constexpr simrv::xlen::Address kAclintMtimerSize =
    static_cast<simrv::xlen::Address>(0x00010000u);
inline constexpr simrv::xlen::PhysAddr kAclintMswiBaseAddress{0x02010000u};
inline constexpr simrv::xlen::Address kAclintMswiSize =
    static_cast<simrv::xlen::Address>(0x00010000u);

inline constexpr simrv::xlen::PhysAddr kAplicMBaseAddress{0x0c000000u};
inline constexpr simrv::xlen::Address kAplicMSize = static_cast<simrv::xlen::Address>(0x00004000u);
inline constexpr simrv::xlen::PhysAddr kAplicSBaseAddress{0x0d000000u};
inline constexpr simrv::xlen::Address kAplicSSize = static_cast<simrv::xlen::Address>(0x00004000u);

inline constexpr simrv::xlen::PhysAddr kImsicMBaseAddress{0x24000000u};
inline constexpr simrv::xlen::Address kImsicMSize = static_cast<simrv::xlen::Address>(0x00010000u);
inline constexpr simrv::xlen::PhysAddr kImsicSBaseAddress{0x28000000u};
inline constexpr simrv::xlen::Address kImsicSSize = static_cast<simrv::xlen::Address>(0x00010000u);

// Parallel NOR Flash (PFlash)
inline constexpr simrv::xlen::PhysAddr kPflash0BaseAddress{SIMRV_PLATFORM_PFLASH0_BASE};
inline constexpr simrv::xlen::Address kPflash0Size =
    static_cast<simrv::xlen::Address>(SIMRV_PLATFORM_PFLASH0_SIZE);
inline constexpr simrv::xlen::PhysAddr kPflash1BaseAddress{SIMRV_PLATFORM_PFLASH1_BASE};
inline constexpr simrv::xlen::Address kPflash1Size =
    static_cast<simrv::xlen::Address>(SIMRV_PLATFORM_PFLASH1_SIZE);

// PCIe ECAM hierarchy and MMIO 32-bit window
inline constexpr simrv::xlen::PhysAddr kPcieEcamBaseAddress{SIMRV_PLATFORM_PCIE_ECAM_BASE};
inline constexpr simrv::xlen::Address kPcieEcamSize =
    static_cast<simrv::xlen::Address>(SIMRV_PLATFORM_PCIE_ECAM_SIZE);
inline constexpr simrv::xlen::PhysAddr kPcieMmioBaseAddress{SIMRV_PLATFORM_PCIE_WINDOW_BASE};
inline constexpr simrv::xlen::Address kPcieMmioSize =
    static_cast<simrv::xlen::Address>(SIMRV_PLATFORM_PCIE_WINDOW_SIZE);

// Standard VirtIO-MMIO v2 base address range (0x10001000..0x10008000)
inline constexpr simrv::xlen::PhysAddr kVirtioMmioBaseAddress{SIMRV_PLATFORM_VIRTIO_BLOCK_BASE};
inline constexpr simrv::xlen::Address kVirtioMmioSlotSize =
    static_cast<simrv::xlen::Address>(SIMRV_PLATFORM_VIRTIO_SLOT_SIZE);

// Standalone DMA Controller (0x10009000..0x1000A000)
inline constexpr simrv::xlen::PhysAddr kDmaControllerBaseAddress{SIMRV_PLATFORM_DMA_BASE};
inline constexpr simrv::xlen::Address kDmaControllerSize =
    static_cast<simrv::xlen::Address>(SIMRV_PLATFORM_DMA_SIZE);
inline constexpr uint32_t kDmaControllerIrq = SIMRV_PLATFORM_DMA_IRQ;

inline constexpr simrv::xlen::PhysAddr kUartBaseAddress{SIMRV_PLATFORM_UART_BASE};
inline constexpr simrv::xlen::Address kUartSize =
    static_cast<simrv::xlen::Address>(SIMRV_PLATFORM_UART_SIZE);

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

inline auto uart_reg(simrv::xlen::PhysAddr p_addr, int8_t& reg_shift_mode,
                     simrv::xlen::PhysAddr base = kUartBaseAddress) -> simrv::xlen::Address {
    const simrv::xlen::Address raw = p_addr.raw() - base.raw();
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
