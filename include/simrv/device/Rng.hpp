/**
 * @file Rng.hpp
 * @brief VirtIO Hardware Random Number Generator (RNG) device.
 */
#pragma once

#include <random>

#include "simrv/device/VirtioDevice.hpp"
#include "simrv/memory/Mmio.hpp"

namespace simrv::device {

/**
 * @class Rng
 * @brief VirtIO hardware random entropy source device (Device ID 4).
 */
class Rng : public VirtioDevice {
   public:
    explicit Rng(simrv::core::Machine& machine);

    static constexpr Address kBaseAddress = static_cast<Address>(0x44000000u);
    static constexpr Address kSize = static_cast<Address>(0x00001000u);

    [[nodiscard]] auto name() const -> const char* override { return "rng"; }
    [[nodiscard]] auto base_address() const -> Address override { return kBaseAddress; }
    [[nodiscard]] auto size() const -> Address override { return kSize; }

    Byte* mmem = nullptr;

   protected:
    [[nodiscard]] auto get_device_id() const -> Word override { return 4; }
    [[nodiscard]] auto get_device_features() const -> Word override { return 0; }
    [[nodiscard]] auto get_queue_num_max() const -> Word override {
        return (QueueSel < virtio::kRngMaxQueueNum) ? virtio::kRngQueueNumMax : 0;
    }
    void process_queue(Word q_idx) override;

   private:
    std::random_device rd_;
};

}  // namespace simrv::device
