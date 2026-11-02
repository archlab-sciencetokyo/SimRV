#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "simrv/core/PerformanceCounters.hpp"

namespace simrv::core {

struct HpmEventDescriptor {
    HpmEvent id;
    std::string_view name;
    std::string_view description;
};

inline constexpr std::array<HpmEventDescriptor, static_cast<std::size_t>(HpmEvent::Count) - 1>
    kHpmEventCatalog{{
#define SIMRV_HPM_EVENT(name, id, label, description) {HpmEvent::name, label, description},
#define SIMRV_HPM_GENERIC_EVENT(code, name)
#include "simrv/core/HpmEventCatalog.def"
#undef SIMRV_HPM_GENERIC_EVENT
#undef SIMRV_HPM_EVENT
    }};

struct SbiGenericHpmMapping {
    uint32_t event_code;
    HpmEvent event;
};
inline constexpr auto kSbiGenericHpmMappings = std::array{
#define SIMRV_HPM_EVENT(name, id, label, description)
#define SIMRV_HPM_GENERIC_EVENT(code, name) SbiGenericHpmMapping{code, HpmEvent::name},
#include "simrv/core/HpmEventCatalog.def"
#undef SIMRV_HPM_GENERIC_EVENT
#undef SIMRV_HPM_EVENT
};

}  // namespace simrv::core
