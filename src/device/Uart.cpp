/**
 * @file Uart.cpp
 * @brief UART device node implementation with TUI dashboard support.
 */
#include "simrv/device/Uart.hpp"

#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#include <thread>

#include "simrv/core/Machine.hpp"
#include "simrv/tui/Tui.hpp"

namespace simrv::device {

namespace {
constexpr int D_UART_IRQ_NUM = 3;
constexpr std::size_t kMaxRxFifoSize = 2048;

void update_uart_irq(simrv::core::Machine& machine, bool uart_rx_ready, Word uart_ier,
                     bool tx_irq_pending) {
    const bool rx_irq_enabled = (uart_ier & static_cast<Word>(0x1U)) != 0;
    const bool tx_irq_enabled = (uart_ier & static_cast<Word>(0x2U)) != 0;
    bool const irq = (rx_irq_enabled && uart_rx_ready) || (tx_irq_enabled && tx_irq_pending);
    machine.cpu.plic_set_irq(D_UART_IRQ_NUM, irq ? 1 : 0);
}

}  // namespace

Uart::Uart(simrv::core::Machine& machine) : machine_(machine) {}

Uart::~Uart() {
    stop_input_thread();
    stop_pty();
}

void Uart::start_input_thread() {
    if (machine_.s_tuimode) return;  // TUI manages its own input
    input_thread_stop_.store(false, std::memory_order_relaxed);
    input_thread_ = std::thread([this]() -> void {
        constexpr int stdin_fd = STDIN_FILENO;
        while (!input_thread_stop_.load(std::memory_order_relaxed)) {
            // Block for up to 20ms waiting for stdin data
            fd_set read_fds;
            struct timeval timeout{.tv_sec = 0, .tv_usec = 20000};
            FD_ZERO(&read_fds);
            FD_SET(stdin_fd, &read_fds);
            if (select(stdin_fd + 1, &read_fds, nullptr, nullptr, &timeout) <= 0) {
                continue;
            }
            if (!FD_ISSET(stdin_fd, &read_fds)) continue;
            uint8_t byte = 0;
            if (::read(stdin_fd, &byte, 1) != 1) continue;

            if (byte == 17) {  // Ctrl-Q → stop simulation
                machine_.is_running_ = false;
                return;
            }
            std::scoped_lock lock(rx_mutex_);
            if (rx_fifo_.size() < kMaxRxFifoSize) {
                rx_fifo_.push(byte);
            }
            const bool has_rx = !rx_fifo_.empty();
            rx_ready_.store(has_rx, std::memory_order_release);
        }
    });
}

void Uart::stop_input_thread() {
    input_thread_stop_.store(true, std::memory_order_relaxed);
    if (input_thread_.joinable()) {
        input_thread_.join();
    }
}

auto Uart::start_pty() -> bool {
    if (pty_.is_open()) return true;
    if (!pty_.open()) return false;

    // Background thread: read bytes typed at the PTY slave and inject them into
    // the guest UART RX FIFO.
    pty_reader_stop_.store(false, std::memory_order_relaxed);
    pty_reader_thread_ = std::thread([this]() -> void {
        uint8_t buf[256];
        while (!pty_reader_stop_.load(std::memory_order_relaxed)) {
            // External terminal input written to the slave is read from the master.
            fd_set fds;
            FD_ZERO(&fds);
            int const master_fd = pty_.master_fd();
            if (master_fd < 0) break;
            FD_SET(master_fd, &fds);
            struct timeval tv{0, 20000};
            int const ret = ::select(master_fd + 1, &fds, nullptr, nullptr, &tv);
            if (ret <= 0) continue;
            ssize_t const n = pty_.read_from_master(buf, sizeof(buf));
            if (n <= 0) continue;
            std::scoped_lock lock(rx_mutex_);
            for (ssize_t i = 0; i < n; ++i) {
                if (rx_fifo_.size() < kMaxRxFifoSize) {
                    rx_fifo_.push(buf[i]);
                }
            }
            rx_ready_.store(true, std::memory_order_release);
        }
    });
    return true;
}

void Uart::stop_pty() {
    pty_reader_stop_.store(true, std::memory_order_relaxed);
    if (pty_reader_thread_.joinable()) {
        pty_reader_thread_.join();
    }
    pty_.close();
}

auto Uart::handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool {
    const Address reg = simrv::mmio::uart_reg(req.address, uart_reg_shift_);
    if (reg == simrv::mmio::kUartRegInvalid) {
        if (req.opcode == memory::TlOpcodeA::Get) resp.data = 0;
        return true;
    }
    const bool dlab_enabled = (uart_lcr_ & simrv::mmio::kUartLcrDlabMask) != 0;

    if (req.opcode == memory::TlOpcodeA::Get) {
        switch (reg) {
            case simrv::mmio::kUartRegRbrThrDll:
                if (dlab_enabled) {
                    resp.data = uart_dll_;
                } else {
                    std::scoped_lock lock(rx_mutex_);
                    if (!rx_fifo_.empty()) {
                        resp.data = static_cast<Word>(rx_fifo_.front());
                        rx_fifo_.pop();
                    } else {
                        resp.data = 0;
                    }
                    const bool has_more = !rx_fifo_.empty();
                    rx_ready_.store(has_more, std::memory_order_release);
                    update_uart_irq(machine_, has_more, uart_ier_, tx_irq_pending_);
                }
                break;
            case simrv::mmio::kUartRegIerDlm:
                resp.data = dlab_enabled ? uart_dlm_ : uart_ier_;
                break;
            case simrv::mmio::kUartRegIirFcr: {
                std::scoped_lock lock(rx_mutex_);
                const bool has_rx = !rx_fifo_.empty();
                Word iir_val = 0x01U;
                if (has_rx) {
                    iir_val = 0x04U;
                } else if (tx_irq_pending_ && ((uart_ier_ & static_cast<Word>(0x2U)) != 0)) {
                    iir_val = 0x02U;
                    tx_irq_pending_ = false;
                    update_uart_irq(machine_, false, uart_ier_, tx_irq_pending_);
                }
                if (fcr_fifo_enabled_) {
                    iir_val |= 0xC0U;
                }
                resp.data = iir_val;
            } break;
            case simrv::mmio::kUartRegLcr:
                resp.data = uart_lcr_;
                break;
            case simrv::mmio::kUartRegMcr:
                resp.data = uart_mcr_;
                break;
            case simrv::mmio::kUartRegLsr: {
                std::scoped_lock lock(rx_mutex_);
                const bool has_rx = !rx_fifo_.empty();
                resp.data = simrv::mmio::kUartLsrThreTemt |
                            (has_rx ? simrv::mmio::kUartLsrDataReady : static_cast<Word>(0));
            } break;
            case simrv::mmio::kUartRegMsr:
                resp.data = static_cast<Word>(0xB0U);
                break;
            case simrv::mmio::kUartRegScr:
                resp.data = uart_scr_;
                break;
            default:
                resp.data = 0;
        }
    } else {
        const Word wdata = req.data;
        switch (reg) {
            case simrv::mmio::kUartRegRbrThrDll:
                if (dlab_enabled) {
                    uart_dll_ = wdata & static_cast<Word>(0xffU);
                } else {
                    const auto ch = static_cast<uint8_t>(wdata & static_cast<Word>(0xffU));
                    // Mirror output to both the built-in TUI and an attached PTY terminal.
                    if (pty_.is_open()) {
                        (void)pty_.write_byte_to_master(ch);
                    }
                    if (machine_.s_tuimode && machine_.tui) {
                        machine_.tui->handle_char_write(static_cast<char>(ch));
                    } else if (!pty_.is_open()) {
                        (void)(::write(STDOUT_FILENO, &ch, 1) == 0);
                    }
                    tx_irq_pending_ = true;
                    std::scoped_lock lock(rx_mutex_);
                    const bool has_rx = !rx_fifo_.empty();
                    update_uart_irq(machine_, has_rx, uart_ier_, tx_irq_pending_);
                }
                break;
            case simrv::mmio::kUartRegIerDlm:
                if (dlab_enabled) {
                    uart_dlm_ = wdata & static_cast<Word>(0xffU);
                } else {
                    uart_ier_ = wdata & static_cast<Word>(0x0fU);
                    if ((uart_ier_ & static_cast<Word>(0x2U)) != 0) {
                        tx_irq_pending_ = true;
                    }
                    std::scoped_lock lock(rx_mutex_);
                    const bool has_rx = !rx_fifo_.empty();
                    update_uart_irq(machine_, has_rx, uart_ier_, tx_irq_pending_);
                }
                break;
            case simrv::mmio::kUartRegIirFcr:
                fcr_fifo_enabled_ = (wdata & static_cast<Word>(0x01U)) != 0;
                if ((wdata & static_cast<Word>(0x02U)) != 0) {
                    std::scoped_lock lock(rx_mutex_);
                    while (!rx_fifo_.empty()) rx_fifo_.pop();
                    rx_ready_.store(false, std::memory_order_release);
                }
                {
                    std::scoped_lock lock(rx_mutex_);
                    const bool has_rx = !rx_fifo_.empty();
                    update_uart_irq(machine_, has_rx, uart_ier_, tx_irq_pending_);
                }
                break;

            case simrv::mmio::kUartRegLcr:
                uart_lcr_ = wdata & static_cast<Word>(0xffU);
                break;
            case simrv::mmio::kUartRegMcr:
                uart_mcr_ = wdata & static_cast<Word>(0x1fU);
                break;
            case simrv::mmio::kUartRegMsr:
                break;
            case simrv::mmio::kUartRegScr:
                uart_scr_ = wdata & static_cast<Word>(0xffU);
                break;
            default:
                break;
        }
    }
    return true;
}

void Uart::push_rx_byte(uint8_t byte) {
    std::scoped_lock lock(rx_mutex_);
    if (rx_fifo_.size() < kMaxRxFifoSize) {
        rx_fifo_.push(byte);
        // Input can arrive on the TUI or PTY reader thread. Only publish readiness here; the
        // simulation thread owns PLIC mutation in non_tui_poll_input().
        rx_ready_.store(true, std::memory_order_release);
    }
}

void Uart::non_tui_poll_input() {
    if (machine_.s_tuimode) {
        bool const has_data = rx_ready_.load(std::memory_order_relaxed);
        update_uart_irq(machine_, has_data, uart_ier_, tx_irq_pending_);
        return;
    }

    if (!rx_ready_.load(std::memory_order_relaxed) && !tx_irq_pending_) {
        return;
    }

    std::scoped_lock lock(rx_mutex_);
    const bool has_rx = !rx_fifo_.empty();
    update_uart_irq(machine_, has_rx, uart_ier_, tx_irq_pending_);
}

auto Uart::is_interrupt_pending() const -> bool {
    std::scoped_lock lock(rx_mutex_);
    const bool rx_irq_enabled = (uart_ier_ & static_cast<Word>(0x1U)) != 0;
    const bool tx_irq_enabled = (uart_ier_ & static_cast<Word>(0x2U)) != 0;
    const bool has_rx = !rx_fifo_.empty();
    return (rx_irq_enabled && has_rx) || (tx_irq_enabled && tx_irq_pending_);
}

}  // namespace simrv::device
