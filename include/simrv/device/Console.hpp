/**
 * @file Console.hpp
 * @brief Virtio console MMIO device interface for TTY-style I/O.
 */
#pragma once

#include "simrv/Define.hpp"
#include "simrv/device/VirtioDevice.hpp"
#include "simrv/memory/Mmio.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::device {

/**
 * @class Console
 * @brief Virtio-based console device for terminal emulation.
 */
class Console : public VirtioDevice {
   public:
    explicit Console(simrv::core::Machine& machine);

    static constexpr Address kBaseAddress = simrv::mmio::kVirtioBaseAddress;
    static constexpr Address kSize = static_cast<Address>(0x00001000u);

    [[nodiscard]] auto name() const -> const char* override { return "console"; }
    [[nodiscard]] auto base_address() const -> Address override { return kBaseAddress; }
    [[nodiscard]] auto size() const -> Address override { return kSize; }

    int MC_receive_input(simrv::core::Machine& machine);

    Byte* mmem = nullptr;
    Byte fifo_en = static_cast<Byte>(0);
    Byte cons_fifo = static_cast<Byte>(0);

   protected:
    [[nodiscard]] auto get_device_id() const -> Word override { return 3; }
    [[nodiscard]] auto get_device_features() const -> Word override { return 0; }
    [[nodiscard]] auto get_queue_num_max() const -> Word override {
        return QueueSel < virtio::kConsoleMaxQueueNum ? QueueNum : 0;
    }
    void process_queue(Word q_idx) override;
};

}  // namespace simrv::device
