/**
 * @file Console.hpp
 * @brief Virtio console MMIO device interface for TTY-style I/O.
 */
#pragma once

#include <mutex>
#include <queue>

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

    auto MC_receive_input(simrv::core::Machine& machine) -> int;

    /// Enqueue a single byte from an external thread (e.g. TUI keyboard input)
    /// for injection into the VirtIO RX ring during the next simulation cycle.
    void push_input_byte(uint8_t byte);

    /// Dequeue one pending byte into fifo_en/cons_fifo for sim-thread injection.
    /// Returns true if a byte was made available.
    auto pop_pending_input() -> bool;

    Byte* mmem = nullptr;
    Byte fifo_en = static_cast<Byte>(0);
    Byte cons_fifo = static_cast<Byte>(0);

   private:
    std::queue<uint8_t> rx_pending_;  // Protected by rx_pending_mutex_
    std::mutex rx_pending_mutex_;

   protected:
    [[nodiscard]] auto get_device_id() const -> Word override { return 3; }
    [[nodiscard]] auto get_device_features() const -> Word override { return 0; }
    [[nodiscard]] auto get_queue_num_max() const -> Word override {
        return QueueSel < virtio::kConsoleMaxQueueNum ? virtio::kConsoleQueueNumMax : 0;
    }
    void process_queue(Word q_idx) override;
};

}  // namespace simrv::device
