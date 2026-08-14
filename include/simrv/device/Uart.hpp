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

#include "simrv/device/PtyBridge.hpp"
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

    /// Open a POSIX PTY for this UART. Returns false if openpty fails.
    [[nodiscard]] auto start_pty() -> bool;
    void stop_pty();
    [[nodiscard]] auto pty_slave_path() const -> const std::string& { return pty_.slave_path(); }
    [[nodiscard]] auto has_pty() const -> bool { return pty_.is_open(); }
    [[nodiscard]] auto pty() -> PtyBridge& { return pty_; }
    [[nodiscard]] auto pty() const -> const PtyBridge& { return pty_; }

   private:
    simrv::core::Machine& machine_;
    int8_t uart_reg_shift_ = -1;
    Word uart_lcr_ = 0;
    Word uart_ier_ = 0;
    Word uart_mcr_ = 0;
    Word uart_scr_ = 0;
    Word uart_dll_ = 0;
    Word uart_dlm_ = 0;
    bool tx_irq_pending_ = false;
    bool fcr_fifo_enabled_ = true;

    std::queue<uint8_t> rx_fifo_;
    std::atomic<bool> rx_ready_{false};
    mutable std::mutex rx_mutex_;
    std::thread input_thread_;
    std::atomic<bool> input_thread_stop_{false};

    PtyBridge pty_;
    std::thread pty_reader_thread_;
    std::atomic<bool> pty_reader_stop_{false};
};
}  // namespace simrv::device