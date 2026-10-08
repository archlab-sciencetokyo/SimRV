#pragma once

#include <array>
#include <memory>
#include <span>
#include <vector>

#include "simrv/device/NetworkBackend.hpp"
#include "simrv/memory/MmioDevice.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::device {

/** CSR/ring-buffer Ethernet MAC adapter backed by SimRV's packet transports. */
class RingBufferEthernetMmio final {
   public:
    RingBufferEthernetMmio(core::Machine& machine, std::string name, Address csr_base,
                           Address csr_size, uint32_t irq, Address rx_base, Address rx_size,
                           Address tx_base, Address tx_size, std::array<uint8_t, 6> mac_address,
                           NetworkBackend::Mode mode);
    ~RingBufferEthernetMmio();
    RingBufferEthernetMmio(const RingBufferEthernetMmio&) = delete;
    auto operator=(const RingBufferEthernetMmio&) -> RingBufferEthernetMmio& = delete;

    [[nodiscard]] auto csr_node() -> memory::TileLinkNode*;
    [[nodiscard]] auto rx_node() -> memory::TileLinkNode*;
    [[nodiscard]] auto tx_node() -> memory::TileLinkNode*;
    void poll_backend();
    void reset();
    [[nodiscard]] auto backend() -> NetworkBackend& { return backend_; }

   private:
    class Window;
    friend class Window;

    auto read_csr(Address offset) -> uint32_t;
    void write_csr(Address offset, uint32_t value);
    auto read_buffer(bool tx, Address offset) -> uint32_t;
    void write_buffer(bool tx, Address offset, uint32_t value);
    void drain_tx();
    void update_rx_irq();
    void receive_packet(std::span<const uint8_t> packet);

    core::Machine& machine_;
    std::string name_;
    Address csr_base_;
    Address csr_size_;
    Address rx_size_;
    Address tx_size_;
    uint32_t irq_;
    NetworkBackend backend_;
    std::array<uint32_t, 7> csr_{};
    std::vector<uint8_t> rx_buffer_;
    std::vector<uint8_t> tx_buffer_;
    std::unique_ptr<Window> csr_window_;
    std::unique_ptr<Window> rx_window_;
    std::unique_ptr<Window> tx_window_;
};

}  // namespace simrv::device
