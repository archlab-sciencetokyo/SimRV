/**
 * @file Uart.hpp
 * @brief UART device node.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>

#include "simrv/core/PlatformMap.h"
#include "simrv/device/PtyBridge.hpp"
#include "simrv/memory/Mmio.hpp"
#include "simrv/memory/TileLinkNode.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::tui {
class Tui;
}

namespace simrv::device {

class Uart : public memory::TileLinkNode {
   public:
    explicit Uart(simrv::core::Machine& machine,
                  simrv::xlen::Address base = simrv::mmio::kUartBaseAddress,
                  simrv::xlen::Address size = simrv::mmio::kUartSize, uint32_t irq = 3,
                  uint64_t clock_hz = SIMRV_PLATFORM_UART_CLOCK_HZ,
                  uint64_t cpu_clock_hz = 160'000'000);
    ~Uart() override;
    [[nodiscard]] auto name() const -> const char* override { return "uart"; }
    [[nodiscard]] auto base_address() const -> simrv::xlen::Address override { return base_; }
    [[nodiscard]] auto size() const -> simrv::xlen::Address override { return size_; }
    auto handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool override;

    /// Consume a pending RX-state transition and publish it to the guest PLIC. Reader threads
    /// only enqueue bytes and mark the transition; interrupt-controller state stays owned by the
    /// simulation thread.
    void service_interrupts();
    void start_input_thread();
    void stop_input_thread();
    [[nodiscard]] auto is_input_thread_running() const -> bool { return input_thread_.joinable(); }
    [[nodiscard]] auto is_interrupt_pending() const -> bool;
    void push_rx_byte(uint8_t byte);
    /// Advance serial transmit and receive state by simulated CPU cycles.
    void advance_cycles(uint64_t cycles);

    /// Open a POSIX PTY for this UART. Returns false if openpty fails.
    [[nodiscard]] auto start_pty() -> bool;
    void stop_pty();
    [[nodiscard]] auto pty_slave_path() const -> const std::string& { return pty_.slave_path(); }
    [[nodiscard]] auto has_pty() const -> bool { return pty_.is_open(); }
    [[nodiscard]] auto pty() -> PtyBridge& { return pty_; }
    [[nodiscard]] auto pty() const -> const PtyBridge& { return pty_; }
    void flush_tx_buffer() noexcept;

   private:
    simrv::core::Machine& machine_;
    simrv::xlen::Address base_;
    simrv::xlen::Address size_;
    uint32_t irq_;
    uint64_t clock_hz_;
    uint64_t cpu_clock_hz_;
    int8_t uart_reg_shift_ = -1;
    uint8_t uart_lcr_ = 0x03;
    uint8_t uart_ier_ = 0;
    uint8_t uart_mcr_ = 0;
    uint8_t uart_scr_ = 0;
    uint8_t uart_dll_ = 2;
    uint8_t uart_dlm_ = 0;
    bool tx_irq_pending_ = false;
    bool fcr_fifo_enabled_ = false;
    uint8_t fcr_trigger_level_ = 1;
    bool rx_overrun_ = false;
    uint64_t tx_phase_ = 0;
    uint64_t rx_phase_ = 0;
    bool tx_shift_active_ = false;
    uint8_t tx_shift_byte_ = 0;
    bool rx_shift_active_ = false;
    uint8_t rx_shift_byte_ = 0;
    std::deque<uint8_t> tx_fifo_;
    std::deque<uint8_t> pending_rx_;

    std::array<uint8_t, 256> tx_buffer_{};
    size_t tx_buffer_len_ = 0;

    std::deque<uint8_t> rx_fifo_;
    std::atomic<bool> irq_update_pending_{false};
    mutable std::mutex rx_mutex_;
    std::thread input_thread_;
    std::atomic<bool> input_thread_stop_{false};

    PtyBridge pty_;
    std::thread pty_reader_thread_;
    std::atomic<bool> pty_reader_stop_{false};
};
}  // namespace simrv::device
