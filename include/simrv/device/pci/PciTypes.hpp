#pragma once

#include <cstdint>
#include <utility>

// PCIe domain types
enum class PciCommandBit : uint16_t {
    IoSpace = 0x0001,
    MemorySpace = 0x0002,
    BusMaster = 0x0004,
    SpecialCycles = 0x0008,
    MemWriteInvalidate = 0x0010,
    VgaPaletteSnoop = 0x0020,
    ParityErrorResponse = 0x0040,
    SserrEnable = 0x0100,
    FastBackToBack = 0x0200,
    InterruptDisable = 0x0400
};

constexpr auto operator|(PciCommandBit lhs, PciCommandBit rhs) noexcept -> PciCommandBit {
    return static_cast<PciCommandBit>(std::to_underlying(lhs) | std::to_underlying(rhs));
}
constexpr auto operator&(PciCommandBit lhs, PciCommandBit rhs) noexcept -> PciCommandBit {
    return static_cast<PciCommandBit>(std::to_underlying(lhs) & std::to_underlying(rhs));
}
constexpr auto operator~(PciCommandBit b) noexcept -> PciCommandBit {
    return static_cast<PciCommandBit>(~std::to_underlying(b));
}
constexpr auto operator|=(PciCommandBit& lhs, PciCommandBit rhs) noexcept -> PciCommandBit& {
    lhs = lhs | rhs;
    return lhs;
}
constexpr auto operator&=(PciCommandBit& lhs, PciCommandBit rhs) noexcept -> PciCommandBit& {
    lhs = lhs & rhs;
    return lhs;
}

enum class PciStatusBit : uint16_t {
    InterruptStatus = 0x0008,
    CapabilitiesList = 0x0010,
    Pci66MHz = 0x0020,
    FastBackToBack = 0x0080,
    MasterDataParityError = 0x0100,
    DevselTiming = 0x0600,
    SignaledTargetAbort = 0x0800,
    ReceivedTargetAbort = 0x1000,
    ReceivedMasterAbort = 0x2000,
    SignaledSystemError = 0x4000,
    DetectedParityError = 0x8000
};

constexpr auto operator|(PciStatusBit lhs, PciStatusBit rhs) noexcept -> PciStatusBit {
    return static_cast<PciStatusBit>(std::to_underlying(lhs) | std::to_underlying(rhs));
}
constexpr auto operator&(PciStatusBit lhs, PciStatusBit rhs) noexcept -> PciStatusBit {
    return static_cast<PciStatusBit>(std::to_underlying(lhs) & std::to_underlying(rhs));
}
constexpr auto operator~(PciStatusBit b) noexcept -> PciStatusBit {
    return static_cast<PciStatusBit>(~std::to_underlying(b));
}
constexpr auto operator|=(PciStatusBit& lhs, PciStatusBit rhs) noexcept -> PciStatusBit& {
    lhs = lhs | rhs;
    return lhs;
}
constexpr auto operator&=(PciStatusBit& lhs, PciStatusBit rhs) noexcept -> PciStatusBit& {
    lhs = lhs & rhs;
    return lhs;
}

enum class PciCapabilityId : uint8_t {
    Null = 0,
    Pmd = 1,
    Agp = 2,
    Vpd = 3,
    SlotId = 4,
    Msi = 5,
    CompactPci = 6,
    PciX = 7,
    HyperTransport = 8,
    VendorSpecific = 9,
    DebugPort = 10,
    CompactPciCentralResource = 11,
    HotPlug = 12,
    SubsystemId = 13,
    Agp8x = 14,
    SecureDevice = 15,
    PciExpress = 16,
    MsiX = 17,
    Sata = 18,
    AdvancedFeatures = 19,
    EnhancedAllocation = 20,
    FlatteningPortal = 21
};
