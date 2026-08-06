/**
 * @file Uart.cpp
 * @brief UART device node implementation with TUI dashboard support.
 */
#include "simrv/device/Uart.hpp"

#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#include <thread>

#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/tui/Tui.hpp"

namespace simrv::device {

namespace {
constexpr int D_UART_IRQ_NUM = 3;

void update_uart_irq(simrv::core::Machine& machine, bool uart_rx_ready, Word uart_ier,
                     bool tx_irq_pending) {
    const bool rx_irq_enabled = (uart_ier & static_cast<Word>(0x1U)) != 0;
    const bool tx_irq_enabled = (uart_ier & static_cast<Word>(0x2U)) != 0;
    bool irq = (rx_irq_enabled && uart_rx_ready) || (tx_irq_enabled && tx_irq_pending);
    machine.cpu.plic_set_irq(D_UART_IRQ_NUM, irq ? 1 : 0);
}

}  // namespace

Uart::Uart(simrv::core::Machine& machine) : machine_(machine) {}

Uart::~Uart() { stop_input_thread(); }

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
            rx_fifo_.push(byte);
        }
    });
}

void Uart::stop_input_thread() {
    input_thread_stop_.store(true, std::memory_order_relaxed);
    if (input_thread_.joinable()) {
        input_thread_.join();
    }
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
                    if (machine_.s_tuimode) {
                        std::scoped_lock lock(rx_mutex_);
                        if (!rx_fifo_.empty()) {
                            resp.data = static_cast<Word>(rx_fifo_.front());
                            rx_fifo_.pop();
                            rx_ready_.store(!rx_fifo_.empty(), std::memory_order_release);
                            update_uart_irq(machine_, !rx_fifo_.empty(), uart_ier_,
                                            tx_irq_pending_);
                        } else {
                            resp.data = 0;
                        }
                    } else {
                        std::scoped_lock lock(rx_mutex_);
                        if (!uart_rx_ready_ && !rx_fifo_.empty()) {
                            uart_rx_byte_ = rx_fifo_.front();
                            rx_fifo_.pop();
                            uart_rx_ready_ = true;
                        }

                        if (uart_rx_ready_) {
                            resp.data = static_cast<Word>(uart_rx_byte_);
                            uart_rx_ready_ = false;
                            if (!rx_fifo_.empty()) {
                                uart_rx_byte_ = rx_fifo_.front();
                                rx_fifo_.pop();
                                uart_rx_ready_ = true;
                            }
                            update_uart_irq(machine_, uart_rx_ready_, uart_ier_, tx_irq_pending_);
                        } else {
                            resp.data = 0;
                        }
                    }
                }
                break;
            case simrv::mmio::kUartRegIerDlm:
                resp.data = dlab_enabled ? uart_dlm_ : uart_ier_;
                break;
            case simrv::mmio::kUartRegIirFcr: {
                const bool rx_ready =
                    machine_.s_tuimode ? rx_ready_.load(std::memory_order_acquire) : uart_rx_ready_;
                if (rx_ready) {
                    resp.data = static_cast<Word>(0x04U);
                } else if (tx_irq_pending_ && ((uart_ier_ & static_cast<Word>(0x2U)) != 0)) {
                    resp.data = static_cast<Word>(0x02U);
                    tx_irq_pending_ = false;
                    update_uart_irq(machine_, rx_ready, uart_ier_, tx_irq_pending_);
                } else {
                    resp.data = static_cast<Word>(0x01U);
                }
            } break;
            case simrv::mmio::kUartRegLcr:
                resp.data = uart_lcr_;
                break;
            case simrv::mmio::kUartRegMcr:
                resp.data = uart_mcr_;
                break;
            case simrv::mmio::kUartRegLsr:
                if (machine_.s_tuimode) {
                    resp.data =
                        simrv::mmio::kUartLsrThreTemt |
                        (rx_ready_.load(std::memory_order_acquire) ? simrv::mmio::kUartLsrDataReady
                                                                   : static_cast<Word>(0));
                } else {
                    std::scoped_lock lock(rx_mutex_);
                    if (!uart_rx_ready_ && !rx_fifo_.empty()) {
                        uart_rx_byte_ = rx_fifo_.front();
                        rx_fifo_.pop();
                        uart_rx_ready_ = true;
                        update_uart_irq(machine_, uart_rx_ready_, uart_ier_, tx_irq_pending_);
                    }

                    resp.data =
                        simrv::mmio::kUartLsrThreTemt |
                        (uart_rx_ready_ ? simrv::mmio::kUartLsrDataReady : static_cast<Word>(0));
                }
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
                    if (machine_.s_tuimode && machine_.tui) {
                        machine_.tui->handle_char_write(static_cast<char>(ch));
                    } else {
                        (void)(::write(STDOUT_FILENO, &ch, 1) == 0);
                    }
                    tx_irq_pending_ = true;
                    const bool rx_ready = machine_.s_tuimode
                                              ? rx_ready_.load(std::memory_order_acquire)
                                              : uart_rx_ready_;
                    update_uart_irq(machine_, rx_ready, uart_ier_, tx_irq_pending_);
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
                    const bool rx_ready = machine_.s_tuimode
                                              ? rx_ready_.load(std::memory_order_acquire)
                                              : uart_rx_ready_;
                    update_uart_irq(machine_, rx_ready, uart_ier_, tx_irq_pending_);
                }
                break;
            case simrv::mmio::kUartRegIirFcr:
                return true;
            case simrv::mmio::kUartRegLcr:
                uart_lcr_ = wdata & static_cast<Word>(0xffU);
                break;
            case simrv::mmio::kUartRegMcr:
                uart_mcr_ = wdata & static_cast<Word>(0x1fU);
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
    rx_fifo_.push(byte);
    rx_ready_.store(true, std::memory_order_release);
}

void Uart::non_tui_poll_input() {
    if (machine_.s_tuimode) {
        bool has_data = rx_ready_.load(std::memory_order_acquire);
        update_uart_irq(machine_, has_data, uart_ier_, tx_irq_pending_);
        return;
    }

    if (simrv::compiler::unlikely(rx_ready_.load(std::memory_order_relaxed) || !rx_fifo_.empty() ||
                                  uart_rx_ready_)) {
        std::scoped_lock lock(rx_mutex_);
        if (!uart_rx_ready_ && !rx_fifo_.empty()) {
            uart_rx_byte_ = rx_fifo_.front();
            rx_fifo_.pop();
            uart_rx_ready_ = true;
            update_uart_irq(machine_, true, uart_ier_, tx_irq_pending_);
        }

        if (uart_rx_ready_ && uart_rx_byte_ == 17) {
            machine_.is_running_ = false;
        }
    }
}

}  // namespace simrv::device