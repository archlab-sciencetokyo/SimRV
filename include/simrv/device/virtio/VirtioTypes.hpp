#pragma once

#include <cstdint>
#include <utility>

// VirtIO domain types
enum class VirtioDeviceStatus : uint8_t {
    Acknowledge = 1,
    Driver = 2,
    DriverOk = 4,
    FeaturesOk = 8,
    DeviceNeedsReset = 64,
    Failed = 128
};
enum class VirtqDescFlag : uint16_t { Next = 1, Write = 2, Indirect = 4 };

constexpr auto operator|(VirtqDescFlag lhs, VirtqDescFlag rhs) noexcept -> VirtqDescFlag {
    return static_cast<VirtqDescFlag>(std::to_underlying(lhs) | std::to_underlying(rhs));
}
constexpr auto operator&(VirtqDescFlag lhs, VirtqDescFlag rhs) noexcept -> VirtqDescFlag {
    return static_cast<VirtqDescFlag>(std::to_underlying(lhs) & std::to_underlying(rhs));
}
constexpr auto operator~(VirtqDescFlag f) noexcept -> VirtqDescFlag {
    return static_cast<VirtqDescFlag>(~std::to_underlying(f));
}
constexpr auto operator|=(VirtqDescFlag& lhs, VirtqDescFlag rhs) noexcept -> VirtqDescFlag& {
    lhs = lhs | rhs;
    return lhs;
}
constexpr auto operator&=(VirtqDescFlag& lhs, VirtqDescFlag rhs) noexcept -> VirtqDescFlag& {
    lhs = lhs & rhs;
    return lhs;
}
