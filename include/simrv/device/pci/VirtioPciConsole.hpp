/**
 * @file VirtioPciConsole.hpp
 * @brief Modern VirtIO-PCI Console Endpoint.
 */
#pragma once

#include "simrv/device/pci/VirtioPciDevice.hpp"
#include "simrv/device/virtio/VirtioCore.hpp"

namespace simrv::device {

/**
 * @class VirtioPciConsole
 * @brief VirtIO-PCI Console Endpoint (Device ID 0x1043 / Subsystem ID 3).
 */
class VirtioPciConsole : public VirtioPciDevice {
   public:
    VirtioPciConsole();

    void send_input(uint8_t ch);
    [[nodiscard]] auto has_input() const -> bool { return backend_.has_rx(); }

   protected:
    auto get_device_features(uint32_t select) -> uint32_t override;
    void on_queue_notify(uint16_t queue_index) override;
    auto read_device_config(Address offset, uint8_t size) -> uint32_t override;

   private:
    virtio::ConsoleBackend backend_;
};

}  // namespace simrv::device
