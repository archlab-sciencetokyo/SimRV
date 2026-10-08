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
    auto get_device_features(uint32_t select) -> uint32_t override;
    auto read_device_config(Address offset, uint8_t size) -> uint32_t override;
    void on_queue_notify(uint16_t queue_index) override;

   private:
    virtio::VirtioFsBackend backend_;
};

}  // namespace simrv::device
