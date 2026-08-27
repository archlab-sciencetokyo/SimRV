// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string_view>

#include "simrv/v3/RunManifest.hpp"

namespace simrv::v3 {

inline constexpr uint32_t kPluginApiVersion = 1;

/// Plugin contract for version 3.  A plugin may provide platform/MMIO devices and lifecycle
/// observers; ISA, pipeline, memory, and UI extension points are intentionally not public.
class Plugin {
   public:
    virtual ~Plugin() = default;
    [[nodiscard]] virtual auto api_version() const noexcept -> uint32_t = 0;
    [[nodiscard]] virtual auto name() const noexcept -> std::string_view = 0;
};

using CreatePlugin = Plugin* (*)();

[[nodiscard]] auto validate_plugin_declaration(const PluginDeclaration& declaration) -> Result<void>;

}  // namespace simrv::v3
