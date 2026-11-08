/**
 * @file SerialDevice.hpp
 * @brief Simulator lifecycle and console interface for a memory-mapped serial device.
 */
#pragma once

#include <cstdint>
#include <string>

#include "simrv/memory/TileLinkNode.hpp"

namespace simrv::device {

/// A serial device owns its MMIO behavior and receives host input through this interface.
/// Input may arrive on a host thread; device interrupt state is published by the simulation thread.
class SerialDevice : public simrv::memory::TileLinkNode {
   public:
    ~SerialDevice() override = default;
    virtual void service_interrupts() = 0;
    virtual void start_input_thread() = 0;
    virtual void stop_input_thread() = 0;
    [[nodiscard]] virtual auto is_input_thread_running() const -> bool = 0;
    [[nodiscard]] virtual auto is_interrupt_pending() const -> bool = 0;
    [[nodiscard]] virtual auto irq_number() const noexcept -> uint32_t = 0;
    virtual void push_rx_byte(uint8_t byte) = 0;
    virtual void advance_cycles(uint64_t cycles) = 0;
    /// True when the device needs a callback at every instruction boundary.
    [[nodiscard]] virtual auto requires_instruction_ticks() const noexcept -> bool { return false; }
    [[nodiscard]] virtual auto has_active_transfer() const noexcept -> bool = 0;
    [[nodiscard]] virtual auto start_pty() -> bool = 0;
    virtual void stop_pty() = 0;
    [[nodiscard]] virtual auto pty_slave_path() const -> const std::string& = 0;
};

}  // namespace simrv::device
