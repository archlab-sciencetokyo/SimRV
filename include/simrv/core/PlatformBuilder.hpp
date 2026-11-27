/**
 * @file PlatformBuilder.hpp
 * @brief Configuration-driven composition of optional PCIe and VirtIO-MMIO platform devices.
 */
#pragma once

#include "simrv/core/MachineConfig.hpp"

namespace simrv::core {

class Machine;

struct PlatformComposition {
    bool pcie = false;
    bool mmio = false;
};

[[nodiscard]] constexpr auto platform_composition(PlatformProfile profile) -> PlatformComposition {
    return {.pcie = profile == PlatformProfile::Pcie, .mmio = profile == PlatformProfile::Mmio};
}

/// Creates the optional platform graph strictly from MachineConfig. Ownership remains in Machine.
class PlatformBuilder final {
   public:
    static void compose(Machine& machine);
};

}  // namespace simrv::core
