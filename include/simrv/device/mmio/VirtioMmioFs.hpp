/** @file VirtioMmioFs.hpp */
#pragma once

#include <string>

#include "simrv/device/mmio/VirtioMmioDevice.hpp"
#include "simrv/device/virtio/VirtioFsBackend.hpp"

namespace simrv::device {

class VirtioMmioFs final : public VirtioMmioDevice {
   public:
    VirtioMmioFs(Address base, uint32_t irq, core::Machine* machine,
                 const std::string& shared_directory, std::string tag = "simrv");
    [[nodiscard]] auto is_ready() const noexcept -> bool { return backend_.is_ready(); }

   protected:
    void on_queue_notify(uint32_t queue_index) override;
    auto read_device_config(Address offset, std::size_t size) -> uint64_t override;

   private:
    virtio::VirtioFsBackend backend_;
};

}  // namespace simrv::device
