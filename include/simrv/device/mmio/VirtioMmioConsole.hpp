/**
 * @file VirtioMmioConsole.hpp
 * @brief VirtIO-MMIO v2 Console Endpoint.
 */
#pragma once

#include "simrv/device/mmio/VirtioMmioDevice.hpp"
#include "simrv/device/virtio/VirtioCore.hpp"

namespace simrv::device {

/**
 * @class VirtioMmioConsole
 * @brief VirtIO-MMIO v2 Console Endpoint.
 */
class VirtioMmioConsole : public VirtioMmioDevice {
   public:
    VirtioMmioConsole(Address base_address, uint32_t irq_num, core::Machine* machine);

    void send_input(uint8_t ch);
    [[nodiscard]] auto has_input() const -> bool { return backend_.has_rx(); }

   protected:
    void on_queue_notify(uint32_t q_idx) override;
    auto read_device_config(Address offset, std::size_t size) -> uint64_t override;

   private:
    virtio::ConsoleBackend backend_;
};

}  // namespace simrv::device
