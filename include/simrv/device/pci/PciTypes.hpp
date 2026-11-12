/**
 * @file PciTypes.hpp
 * @brief PCI/PCIe domain types, capability enumerations, and BDF identifiers.
 */
#pragma once

#include <compare>
#include <concepts>
#include <cstdint>
#include <utility>

#include "simrv/memory/MmioTypes.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::device {

template <typename Tag, std::integral Rep>
struct PciValue {
    constexpr PciValue() noexcept = default;
    constexpr explicit PciValue(Rep value) noexcept : val(value) {}

    [[nodiscard]] constexpr auto raw() const noexcept -> Rep { return val; }
    [[nodiscard]] constexpr explicit operator Rep() const noexcept { return val; }

    constexpr auto operator<=>(const PciValue&) const noexcept = default;
    constexpr bool operator==(const PciValue&) const noexcept = default;

   private:
    Rep val{};
};

// Domain type aliases for PCI configuration & endpoints
struct PciVendorIdTag;
struct PciDeviceIdTag;
struct PciSubsystemVendorIdTag;
struct PciSubsystemIdTag;
struct PciRevisionIdTag;
struct PciClassCodeTag;
using PciVendorId = PciValue<PciVendorIdTag, uint16_t>;
using PciDeviceId = PciValue<PciDeviceIdTag, uint16_t>;
using PciSubsystemVendorId = PciValue<PciSubsystemVendorIdTag, uint16_t>;
using PciSubsystemId = PciValue<PciSubsystemIdTag, uint16_t>;
using PciRevisionId = PciValue<PciRevisionIdTag, uint8_t>;
using PciClassCode = PciValue<PciClassCodeTag, uint32_t>;

struct PciBusNumberTag;
struct PciDeviceNumberTag;
struct PciFunctionNumberTag;
struct PciConfigOffsetTag;

using PciBusNumber = PciValue<PciBusNumberTag, uint8_t>;
using PciDeviceNumber = PciValue<PciDeviceNumberTag, uint8_t>;
using PciFunctionNumber = PciValue<PciFunctionNumberTag, uint8_t>;

using BarIndex = uint8_t;
using PciBarIndex = BarIndex;
using PciBarOffset = simrv::memory::MmioOffset;
using PciBarSize = uint64_t;
using PciBarMask = uint64_t;

using PciConfigOffset = PciValue<PciConfigOffsetTag, uint16_t>;
using PciAccessBytes = uint8_t;
using PciConfigData = uint32_t;
using PciMmioData = simrv::xlen::Word;

static_assert(sizeof(PciBusNumber) == sizeof(uint8_t));
static_assert(sizeof(PciDeviceNumber) == sizeof(uint8_t));
static_assert(sizeof(PciFunctionNumber) == sizeof(uint8_t));
static_assert(sizeof(PciConfigOffset) == sizeof(uint16_t));
static_assert(sizeof(PciVendorId) == sizeof(uint16_t));
static_assert(sizeof(PciDeviceId) == sizeof(uint16_t));
static_assert(sizeof(PciSubsystemVendorId) == sizeof(uint16_t));
static_assert(sizeof(PciSubsystemId) == sizeof(uint16_t));
static_assert(sizeof(PciRevisionId) == sizeof(uint8_t));
static_assert(sizeof(PciClassCode) == sizeof(uint32_t));

/**
 * @struct PciBdf
 * @brief Bus / Device / Function PCI topology coordinate.
 */
struct PciBdf {
    PciBusNumber bus{0};
    PciDeviceNumber dev{0};
    PciFunctionNumber func{0};

    [[nodiscard]] constexpr auto raw_address() const noexcept -> uint32_t {
        return (static_cast<uint32_t>(bus.raw()) << 20) | (static_cast<uint32_t>(dev.raw()) << 15) |
               (static_cast<uint32_t>(func.raw()) << 12);
    }
    constexpr auto operator<=>(const PciBdf&) const noexcept = default;
    constexpr bool operator==(const PciBdf&) const noexcept = default;
};

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

}  // namespace simrv::device
