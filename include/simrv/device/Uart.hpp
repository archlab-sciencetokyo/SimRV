/**
 * @file Uart.hpp
 * @brief UART device node.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <queue>
#include <thread>

#include "simrv/memory/Mmio.hpp"
#include "simrv/memory/TileLinkNode.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::tui {
class Tui;
}

namespace simrv::device {

class Uart : public memory::TileLinkNode {
   public:
    explicit Uart(simrv::core::Machine& machine);
    ~Uart() override;
    [[nodiscard]] auto name() const -> const char* override { return "uart"; }
    [[nodiscard]] auto base_address() const -> Address override {
        return simrv::mmio::kUartBaseAddress;
    }
    [[nodiscard]] auto size() const -> Address override { return simrv::mmio::kUartSize; }
    auto handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool override;

    void non_tui_poll_input();
    void start_input_thread();
    void stop_input_thread();
    [[nodiscard]] auto is_input_thread_running() const -> bool { return input_thread_.joinable(); }
    [[nodiscard]] auto is_interrupt_pending() const -> bool;
    void push_rx_byte(uint8_t byte);

   private:
    simrv::core::Machine& machine_;
    int8_t uart_reg_shift_ = -1;
    Word uart_lcr_ = 0;
    Word uart_ier_ = 0;
    Word uart_mcr_ = 0;
    Word uart_scr_ = 0;
    Word uart_dll_ = 0;
    Word uart_dlm_ = 0;
    bool uart_rx_ready_ = false;
    bool tx_irq_pending_ = false;
    bool fcr_fifo_enabled_ = true;
    uint8_t uart_rx_byte_ = 0;

    std::queue<uint8_t> rx_fifo_;
    std::atomic<bool> rx_ready_{false};
    mutable std::mutex rx_mutex_;
    std::thread input_thread_;
    std::atomic<bool> input_thread_stop_{false};
};
}  // namespace simrv::device