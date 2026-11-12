/**
 * @file FramebufferDevice.hpp
 * @brief Display interface for memory-mapped or plugin-backed framebuffer devices.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "simrv/core/MachineConfig.hpp"
#include "simrv/memory/RamView.hpp"

namespace simrv::device {

/**
 * @class FramebufferDevice
 * @brief Provides a uniform pixel source and geometry for simulator TUI and graphical sinks.
 */
class FramebufferDevice {
   public:
    virtual ~FramebufferDevice() = default;

    [[nodiscard]] virtual auto framebuffer_geometry() const noexcept
        -> simrv::core::FramebufferConfig = 0;
    [[nodiscard]] virtual auto framebuffer_data() const noexcept
        -> std::span<const uint8_t> = 0;
    [[nodiscard]] virtual auto is_display_active() const noexcept -> bool {
        return !framebuffer_data().empty();
    }
    [[nodiscard]] virtual auto frame_id() const noexcept -> uint64_t { return 0; }
};

}  // namespace simrv::device
