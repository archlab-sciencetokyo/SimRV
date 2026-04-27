/**
 * @file MmioRouter.cpp
 * @brief SimRV MMIO router implementation.
 */
#include "MmioRouter.hpp"

#include <bits/types/struct_timeval.h>
#include <sys/select.h>
#include <unistd.h>

#include <algorithm>

#include <print>
#include <cstdint>
#include <cstdio>

#include "Machine.hpp"
#include "XLen.hpp"

namespace {
constexpr Address D_TOHOST_ADDR = static_cast<Address>(0x40008000U);
constexpr Address D_UART_BASE_ADDR = static_cast<Address>(0x10000000U);
constexpr Address D_UART_SIZE = static_cast<Address>(0x00000100U);

constexpr Address D_UART_REG_RBR_THR_DLL = static_cast<Address>(0x00U);
constexpr Address D_UART_REG_IER_DLM = static_cast<Address>(0x01U);
constexpr Address D_UART_REG_IIR_FCR = static_cast<Address>(0x02U);
constexpr Address D_UART_REG_LCR = static_cast<Address>(0x03U);
constexpr Address D_UART_REG_MCR = static_cast<Address>(0x04U);
constexpr Address D_UART_REG_LSR = static_cast<Address>(0x05U);
constexpr Address D_UART_REG_SCR = static_cast<Address>(0x07U);

constexpr Word D_UART_LCR_DLAB_MASK = static_cast<Word>(0x80U);
constexpr Word D_UART_LSR_DATA_READY = static_cast<Word>(0x01U);
constexpr Word D_UART_LSR_THRE_TEMT = static_cast<Word>(0x60U);
constexpr int D_UART_IRQ_NUM = 3;

auto is_uart_addr(Address p_addr) -> bool {
    return p_addr >= D_UART_BASE_ADDR && p_addr < (D_UART_BASE_ADDR + D_UART_SIZE);
}

constexpr Address D_UART_REG_INVALID = (~Address{0});

auto decode_uart_reg_byte(Address raw) -> Address {
    switch (raw & static_cast<Address>(0x07U)) {
        case static_cast<Address>(0x00U):
            return D_UART_REG_RBR_THR_DLL;
        case static_cast<Address>(0x01U):
            return D_UART_REG_IER_DLM;
        case static_cast<Address>(0x02U):
            return D_UART_REG_IIR_FCR;
        case static_cast<Address>(0x03U):
            return D_UART_REG_LCR;
        case static_cast<Address>(0x04U):
            return D_UART_REG_MCR;
        case static_cast<Address>(0x05U):
            return D_UART_REG_LSR;
        case static_cast<Address>(0x07U):
            return D_UART_REG_SCR;
        default:
            return D_UART_REG_INVALID;
    }
}

auto decode_uart_reg_word(Address raw) -> Address {
    // Word-stride access layout (offsets 0x00,0x04,0x08,...)
    switch (raw) {
        case static_cast<Address>(0x00U):
            return D_UART_REG_RBR_THR_DLL;
        case static_cast<Address>(0x04U):
            return D_UART_REG_IER_DLM;
        case static_cast<Address>(0x08U):
            return D_UART_REG_IIR_FCR;
        case static_cast<Address>(0x0CU):
            return D_UART_REG_LCR;
        case static_cast<Address>(0x10U):
            return D_UART_REG_MCR;
        case static_cast<Address>(0x14U):
            return D_UART_REG_LSR;
        case static_cast<Address>(0x1CU):
            return D_UART_REG_SCR;
        default:
            return D_UART_REG_INVALID;
    }
}

auto uart_reg(Address p_addr, int8_t& reg_shift_mode) -> Address {
    const Address raw = p_addr - D_UART_BASE_ADDR;
    if (reg_shift_mode < 0) {
        // Byte-stride unique offsets immediately lock byte mode.
        if (raw == static_cast<Address>(0x01U) || raw == static_cast<Address>(0x02U) ||
            raw == static_cast<Address>(0x03U) || raw == static_cast<Address>(0x05U) ||
            raw == static_cast<Address>(0x07U)) {
            reg_shift_mode = 0;
        }

        // Word-stride unique offsets lock word mode.
        if (raw == static_cast<Address>(0x08U) || raw == static_cast<Address>(0x0CU) ||
            raw == static_cast<Address>(0x10U) || raw == static_cast<Address>(0x14U) ||
            raw == static_cast<Address>(0x1CU)) {
            reg_shift_mode = 2;
        }

        // Ambiguous early offsets default to byte mode for ns16550 DT bindings (reg-shift=0).
        reg_shift_mode = std::max<int8_t>(reg_shift_mode, 0);
    }

    if (reg_shift_mode == 2) {
        return decode_uart_reg_word(raw);
    }

    return decode_uart_reg_byte(raw);
}

void maybe_log_mmio(Machine& machine, const char* op, const char* dev, Address p_addr, Word value) {
    static int mmio_log_count = 0;
    if (!machine.s_debugmode || mmio_log_count >= 64) {
        return;
    }
    std::println("__ {:10} MMIO {:5} {:7} addr={:08x} data={:08x}", machine.cpu.mtime, op, dev,
                static_cast<unsigned>(p_addr), static_cast<unsigned>(value));
    ++mmio_log_count;
}

void maybe_log_mmio_unhandled(Machine& machine, const char* op, Address p_addr, Word value) {
    static int unhandled_log_count = 0;
    if (!machine.s_debugmode || unhandled_log_count >= 64) {
        return;
    }
    std::println("__ {:10} MMIO {:5} {:7} addr={:08x} data={:08x}", machine.cpu.mtime, op,
                "unhandled", static_cast<unsigned>(p_addr), static_cast<unsigned>(value));
    ++unhandled_log_count;
}

void update_uart_irq(Machine& machine, bool uart_rx_ready, Word uart_ier) {
    const bool rx_irq_enabled = (uart_ier & static_cast<Word>(0x1U)) != 0;
    machine.cpu.plic_set_irq(D_UART_IRQ_NUM, uart_rx_ready && rx_irq_enabled ? 1 : 0);
}

auto poll_uart_rx(uint8_t& byte_out) -> bool {
    constexpr int stdin_fd = STDIN_FILENO;
    fd_set read_fds;
    struct timeval timeout{.tv_sec=0, .tv_usec=0};

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

MmioRouter::MmioRouter(Machine& machine) : machine_(machine) {}

auto MmioRouter::read(Address p_addr, Word& rdata) -> bool {
    if (is_uart_addr(p_addr)) {
        maybe_log_mmio(machine_, "read", "uart", p_addr, 0);
        const Address reg = uart_reg(p_addr, uart_reg_shift_);
        if (reg == D_UART_REG_INVALID) {
            rdata = 0;
            return true;
        }
        const bool dlab_enabled = (uart_lcr_ & D_UART_LCR_DLAB_MASK) != 0;
        switch (reg) {
            case D_UART_REG_RBR_THR_DLL:
                if (dlab_enabled) {
                    rdata = uart_dll_;
                } else {
                    if (!uart_rx_ready_) {
                        uart_rx_ready_ = poll_uart_rx(uart_rx_byte_);
                        update_uart_irq(machine_, uart_rx_ready_, uart_ier_);
                    }
                    if (uart_rx_ready_) {
                        rdata = static_cast<Word>(uart_rx_byte_);
                        uart_rx_ready_ = false;
                        update_uart_irq(machine_, false, uart_ier_);
                    } else {
                        rdata = 0;
                    }
                }
                return true;
            case D_UART_REG_IER_DLM:
                rdata = dlab_enabled ? uart_dlm_ : uart_ier_;
                return true;
            case D_UART_REG_IIR_FCR:
                rdata = static_cast<Word>(0x01U);  // No pending interrupts.
                return true;
            case D_UART_REG_LCR:
                rdata = uart_lcr_;
                return true;
            case D_UART_REG_MCR:
                rdata = uart_mcr_;
                return true;
            case D_UART_REG_LSR:
                if (!uart_rx_ready_) {
                    uart_rx_ready_ = poll_uart_rx(uart_rx_byte_);
                    update_uart_irq(machine_, uart_rx_ready_, uart_ier_);
                }
                rdata = D_UART_LSR_THRE_TEMT |
                        (uart_rx_ready_ ? D_UART_LSR_DATA_READY : static_cast<Word>(0));
                return true;
            case D_UART_REG_SCR:
                rdata = uart_scr_;
                return true;
            default:
                rdata = 0;
                return true;
        }
    }

    switch (p_addr & static_cast<Address>(0xF0000000U)) {
        case static_cast<Address>(0x40000000U):
            if (machine_.console != nullptr && machine_.console->contains(p_addr)) {
                maybe_log_mmio(machine_, "read", "virtio-c", p_addr, 0);
                return machine_.console->read(machine_, p_addr, rdata);
            }
            if (machine_.disk != nullptr && machine_.disk->contains(p_addr)) {
                maybe_log_mmio(machine_, "read", "virtio-d", p_addr, 0);
                return machine_.disk->read(machine_, p_addr, rdata);
            }
            maybe_log_mmio_unhandled(machine_, "read", p_addr, 0);
            return false;
        case static_cast<Address>(0x50000000U):
            maybe_log_mmio(machine_, "read", "plic", p_addr, 0);
            return machine_.cpu.plic_mmio.contains(p_addr) &&
                   machine_.cpu.plic_mmio.read(machine_, p_addr, rdata);
        case static_cast<Address>(0x60000000U):
            maybe_log_mmio(machine_, "read", "clint", p_addr, 0);
            return machine_.cpu.clint_mmio.contains(p_addr) &&
                   machine_.cpu.clint_mmio.read(machine_, p_addr, rdata);
        default:
            maybe_log_mmio_unhandled(machine_, "read", p_addr, 0);
            return false;
    }
}

auto MmioRouter::write(Address p_addr, Word wdata) -> bool {
    if (p_addr == D_TOHOST_ADDR) {
        maybe_log_mmio(machine_, "write", "tohost", p_addr, wdata);
        machine_.tohost = wdata;
        return true;
    }

    if (is_uart_addr(p_addr)) {
        maybe_log_mmio(machine_, "write", "uart", p_addr, wdata);
        const Address reg = uart_reg(p_addr, uart_reg_shift_);
        if (reg == D_UART_REG_INVALID) {
            return true;
        }
        const bool dlab_enabled = (uart_lcr_ & D_UART_LCR_DLAB_MASK) != 0;
        switch (reg) {
            case D_UART_REG_RBR_THR_DLL:
                if (dlab_enabled) {
                    uart_dll_ = wdata & static_cast<Word>(0xffU);
                } else {
                    const auto ch = static_cast<uint8_t>(wdata & static_cast<Word>(0xffU));
                    (void)(::write(STDOUT_FILENO, &ch, 1) == 0);
                    if (machine_.s_debugmode) {
                        std::println("__ {:10} UART tx {:02x}", machine_.cpu.mtime,
                               static_cast<unsigned>(ch));
                    }
                }
                return true;
            case D_UART_REG_IER_DLM:
                if (dlab_enabled) {
                    uart_dlm_ = wdata & static_cast<Word>(0xffU);
                } else {
                    uart_ier_ = wdata & static_cast<Word>(0x0fU);
                    update_uart_irq(machine_, uart_rx_ready_, uart_ier_);
                }
                return true;
            case D_UART_REG_IIR_FCR:
                return true;
            case D_UART_REG_LCR:
                uart_lcr_ = wdata & static_cast<Word>(0xffU);
                return true;
            case D_UART_REG_MCR:
                uart_mcr_ = wdata & static_cast<Word>(0x1fU);
                return true;
            case D_UART_REG_SCR:
                uart_scr_ = wdata & static_cast<Word>(0xffU);
                return true;
            default:
                return true;
        }
    }

    switch (p_addr & static_cast<Address>(0xF0000000U)) {
        case static_cast<Address>(0x40000000U):
            if (machine_.console != nullptr && machine_.console->contains(p_addr)) {
                maybe_log_mmio(machine_, "write", "virtio-c", p_addr, wdata);
                return machine_.console->write(machine_, p_addr, wdata);
            }
            if (machine_.disk != nullptr && machine_.disk->contains(p_addr)) {
                maybe_log_mmio(machine_, "write", "virtio-d", p_addr, wdata);
                return machine_.disk->write(machine_, p_addr, wdata);
            }
            maybe_log_mmio_unhandled(machine_, "write", p_addr, wdata);
            return false;
        case static_cast<Address>(0x50000000U):
            maybe_log_mmio(machine_, "write", "plic", p_addr, wdata);
            return machine_.cpu.plic_mmio.contains(p_addr) &&
                   machine_.cpu.plic_mmio.write(machine_, p_addr, wdata);
        case static_cast<Address>(0x60000000U):
            maybe_log_mmio(machine_, "write", "clint", p_addr, wdata);
            return machine_.cpu.clint_mmio.contains(p_addr) &&
                   machine_.cpu.clint_mmio.write(machine_, p_addr, wdata);
        default:
            maybe_log_mmio_unhandled(machine_, "write", p_addr, wdata);
            return false;
    }
}
