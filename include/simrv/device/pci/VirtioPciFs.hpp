/** @file VirtioPciFs.hpp */
#pragma once

#include <string>

#include "simrv/device/pci/VirtioPciDevice.hpp"
#include "simrv/device/virtio/VirtioFsBackend.hpp"

namespace simrv::device {

class VirtioPciFs final : public VirtioPciDevice {
   public:
    explicit VirtioPciFs(const std::string& shared_directory, std::string tag = "simrv");
    [[nodiscard]] auto is_ready() const noexcept -> bool { return backend_.is_ready(); }

   protected:
    auto read_device_config(PciBarOffset offset, PciAccessBytes size) -> PciMmioData override;
    void on_queue_notify(virtio::VirtioQueueIndex queue_index) override;

   private:
    virtio::VirtioFsBackend backend_;
};

}  // namespace simrv::device
