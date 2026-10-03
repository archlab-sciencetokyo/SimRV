/**
 * @file VirtioMmioSound.hpp
 * @brief VirtIO-MMIO v2 Audio/Sound Endpoint.
 */
#pragma once

#include "simrv/device/mmio/VirtioMmioDevice.hpp"
#include "simrv/device/virtio/VirtioSoundBackend.hpp"

namespace simrv::device {

/**
 * @class VirtioMmioSound
 * @brief VirtIO-MMIO v2 Sound Endpoint.
 */
class VirtioMmioSound : public VirtioMmioDevice {
   public:
    VirtioMmioSound(Address base_address, uint32_t irq_num, core::Machine* machine);

    [[nodiscard]] auto backend() noexcept -> virtio::VirtioSoundBackend& { return backend_; }
    [[nodiscard]] auto backend() const noexcept -> const virtio::VirtioSoundBackend& {
        return backend_;
    }

   protected:
    void on_queue_notify(uint32_t q_idx) override;
    auto read_device_config(Address offset, std::size_t size) -> uint64_t override;

   private:
    virtio::VirtioSoundBackend backend_{};
};

}  // namespace simrv::device
