/**
 * @file MachineConfig.hpp
 * @brief Value configuration shared by machine, platform, and memory setup.
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "simrv/memory/MemoryUtil.hpp"

namespace simrv::core {

enum class PlatformProfile : uint8_t {
    Pcie = 0,
    Mmio = 1,
    Hybrid = 2,
};

struct MemoryGeometry {
    Address dram_base = memory::kDramBaseAddress;
    Address dram_size = memory::kDramSize;

    [[nodiscard]] constexpr auto contains(Address address, size_t size = 1) const -> bool {
        return memory::address_range_contains(dram_base, dram_size, address, size);
    }
};

struct MachineConfig {
    MemoryGeometry memory{};
    unsigned core_count = SIMRV_CORE_COUNT;
    unsigned disk_size_mb = SIMRV_DISK_SIZE_MB;
    PlatformProfile platform_profile = PlatformProfile::Pcie;
};

}  // namespace simrv::core
