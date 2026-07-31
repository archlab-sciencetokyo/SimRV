/**
 * @file VirtioDevice.hpp
 * @brief Base class for VirtIO MMIO devices.
 */
#pragma once

#include "simrv/device/Virtio.hpp"
#include "simrv/memory/TileLinkNode.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::device {

class VirtioDevice : public memory::TileLinkNode {
   public:
    explicit VirtioDevice(simrv::core::Machine& machine, Word irq, Word max_queues);
    ~VirtioDevice() override = default;

    auto handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool override;

    virtio::QueueState* Queue = nullptr;

    Word DeviceFeaturesSel = 0;
    Word DriverFeatures = 0;
    Word DriverFeaturesSel = 0;
    Word InterruptStatus = 0;
    Word Status = 0;
    Word QueueSel = 0;
    Word QueueNum = 0;

   protected:
    simrv::core::Machine& machine_;  // NOLINT
    Word irq_;
    Word max_queues_ = 0;

    [[nodiscard]] virtual auto get_device_id() const -> Word = 0;
    [[nodiscard]] virtual auto get_vendor_id() const -> Word { return virtio::kVendorId; }
    [[nodiscard]] virtual auto get_device_features() const -> Word = 0;
    [[nodiscard]] virtual auto get_queue_num_max() const -> Word = 0;
    [[nodiscard]] virtual auto get_config_generation() const -> Word { return 0; }

    virtual void process_queue(Word q_idx) = 0;

    [[nodiscard]] virtual auto read_config(Address /*offset*/) const -> Word { return 0; }
    virtual void write_config(Address /*offset*/, Word /*wdata*/) {}

    void trigger_interrupt();

   private:
    [[nodiscard]] auto mmio_read(Address offset) const -> Word;
    void mmio_write(Address offset, Word wdata);
};

}  // namespace simrv::device