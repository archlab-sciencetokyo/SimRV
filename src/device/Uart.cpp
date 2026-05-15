/**
 * @file Uart.cpp
 * @brief UART device node implementation.
 */
#include "simrv/device/Uart.hpp"

#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#include "simrv/core/Machine.hpp"

namespace simrv::device {

namespace {
constexpr int D_UART_IRQ_NUM = 3;

void update_uart_irq(simrv::core::Machine& machine, bool uart_rx_ready, Word uart_ier) {
    const bool rx_irq_enabled = (uart_ier & static_cast<Word>(0x1U)) != 0;
    machine.cpu.plic_set_irq(D_UART_IRQ_NUM, uart_rx_ready && rx_irq_enabled ? 1 : 0);
}

auto poll_uart_rx(uint8_t& byte_out) -> bool {
    constexpr int stdin_fd = STDIN_FILENO;
    fd_set read_fds;
    struct timeval timeout{.tv_sec = 0, .tv_usec = 0};
    FD_ZERO(&read_fds);
    FD_SET(stdin_fd, &read_fds);
    if (select(stdin_fd + 1, &read_fds, nullptr, nullptr, &timeout) <= 0) {
        return false;
    }
    if (!FD_ISSET(stdin_fd, &read_fds)) {
        return false;
    }
    uint8_t byte = 0;
    if (::read(stdin_fd, &byte, 1) != 1) {
        return false;
    }
    byte_out = byte;
    return true;
}
}  // namespace

Uart::Uart(simrv::core::Machine& machine) : machine_(machine) {}

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
                    if (!uart_rx_ready_) {
                        uart_rx_ready_ = poll_uart_rx(uart_rx_byte_);
                        update_uart_irq(machine_, uart_rx_ready_, uart_ier_);
                    }
                    if (uart_rx_ready_) {
                        resp.data = static_cast<Word>(uart_rx_byte_);
                        uart_rx_ready_ = false;
                        update_uart_irq(machine_, false, uart_ier_);
                    } else {
                        resp.data = 0;
                    }
                }
                break;
            case simrv::mmio::kUartRegIerDlm:
                resp.data = dlab_enabled ? uart_dlm_ : uart_ier_;
                break;
            case simrv::mmio::kUartRegIirFcr:
                resp.data = static_cast<Word>(0x01U);
                break;
            case simrv::mmio::kUartRegLcr:
                resp.data = uart_lcr_;
                break;
            case simrv::mmio::kUartRegMcr:
                resp.data = uart_mcr_;
                break;
            case simrv::mmio::kUartRegLsr:
                if (!uart_rx_ready_) {
                    uart_rx_ready_ = poll_uart_rx(uart_rx_byte_);
                    update_uart_irq(machine_, uart_rx_ready_, uart_ier_);
                }
                resp.data =
                    simrv::mmio::kUartLsrThreTemt |
                    (uart_rx_ready_ ? simrv::mmio::kUartLsrDataReady : static_cast<Word>(0));
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
                    (void)(::write(STDOUT_FILENO, &ch, 1) == 0);
                }
                break;
            case simrv::mmio::kUartRegIerDlm:
                if (dlab_enabled) {
                    uart_dlm_ = wdata & static_cast<Word>(0xffU);
                } else {
                    uart_ier_ = wdata & static_cast<Word>(0x0fU);
                    update_uart_irq(machine_, uart_rx_ready_, uart_ier_);
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
}  // namespace simrv::device