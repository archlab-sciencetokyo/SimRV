/**
 * @file Uart.cpp
 * @brief UART device node implementation with TUI dashboard support.
 */
#include "simrv/device/Uart.hpp"

#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

#include <charconv>
#include <chrono>
#include <string_view>
#include <thread>

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

Uart::Uart(simrv::core::Machine& machine) : machine_(machine) {
    if (machine_.s_tuimode) {
        tui_ = std::make_unique<simrv::tui::Tui>(machine_);
    }
}

Uart::~Uart() = default;

auto Uart::parse_sgr_mouse(const std::string& seq, int& b, int& x, int& y) -> bool {
    // Expected SGR mouse format: ESC [ < b ; x ; y M|m
    if (seq.size() < 7 || seq[0] != '\x1b' || seq[1] != '[' || seq[2] != '<') {
        return false;
    }

    const char tail = seq.back();
    if (tail != 'M' && tail != 'm') {
        return false;
    }

    std::string_view payload(seq.data() + 3, seq.size() - 4);
    const std::size_t semi1 = payload.find(';');
    if (semi1 == std::string_view::npos) {
        return false;
    }
    const std::size_t semi2 = payload.find(';', semi1 + 1);
    if (semi2 == std::string_view::npos) {
        return false;
    }

    const std::string_view b_text = payload.substr(0, semi1);
    const std::string_view x_text = payload.substr(semi1 + 1, semi2 - semi1 - 1);
    const std::string_view y_text = payload.substr(semi2 + 1);

    auto parse_int = [](std::string_view text, int& out) -> bool {
        if (text.empty()) {
            return false;
        }
        const char* first = text.data();
        const char* last = text.data() + text.size();
        const auto result = std::from_chars(first, last, out);
        return result.ec == std::errc{} && result.ptr == last;
    };

    if (!parse_int(b_text, b) || !parse_int(x_text, x) || !parse_int(y_text, y)) {
        return false;
    }

    return true;
}

auto Uart::consume_tui_control_sequence(uint8_t first_byte) -> bool {
    if (!tui_ || first_byte != 0x1b) {
        return false;
    }

    esc_buf_.clear();
    esc_buf_.push_back(static_cast<char>(first_byte));

    constexpr int kMaxSeqLen = 64;
    uint8_t byte = 0;
    while (static_cast<int>(esc_buf_.size()) < kMaxSeqLen && poll_uart_rx(byte)) {
        esc_buf_.push_back(static_cast<char>(byte));
        if (byte == 'M' || byte == 'm' || byte == '~' || byte == 'A' || byte == 'B' ||
            byte == 'C' || byte == 'D') {
            break;
        }
    }

    int button = 0;
    int x = 0;
    int y = 0;
    if (parse_sgr_mouse(esc_buf_, button, x, y)) {
        tui_->handle_mouse(x, y, button);
        return true;
    }

    // Non-mouse escape sequence consumed deliberately in TUI mode.
    return true;
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
                        if (!rx_fifo_.empty()) {
                            resp.data = static_cast<Word>(rx_fifo_.front());
                            rx_fifo_.pop();
                            update_uart_irq(machine_, !rx_fifo_.empty(), uart_ier_,
                                            tx_irq_pending_);
                        } else {
                            resp.data = 0;
                        }
                    } else {
                        if (!uart_rx_ready_) {
                            uart_rx_ready_ = poll_uart_rx(uart_rx_byte_);
                            update_uart_irq(machine_, uart_rx_ready_, uart_ier_, tx_irq_pending_);
                        }
                        if (uart_rx_ready_) {
                            resp.data = static_cast<Word>(uart_rx_byte_);
                            uart_rx_ready_ = false;
                            update_uart_irq(machine_, false, uart_ier_, tx_irq_pending_);
                        } else {
                            resp.data = 0;
                        }
                    }
                }
                break;
            case simrv::mmio::kUartRegIerDlm:
                resp.data = dlab_enabled ? uart_dlm_ : uart_ier_;
                break;
            case simrv::mmio::kUartRegIirFcr:
                if (uart_rx_ready_) {
                    resp.data = static_cast<Word>(0x04U);
                } else if (tx_irq_pending_ && ((uart_ier_ & static_cast<Word>(0x2U)) != 0)) {
                    resp.data = static_cast<Word>(0x02U);
                    tx_irq_pending_ = false;
                    update_uart_irq(machine_, uart_rx_ready_, uart_ier_, tx_irq_pending_);
                } else {
                    resp.data = static_cast<Word>(0x01U);
                }
                break;
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
                        (!rx_fifo_.empty() ? simrv::mmio::kUartLsrDataReady : static_cast<Word>(0));
                } else {
                    if (!uart_rx_ready_) {
                        uart_rx_ready_ = poll_uart_rx(uart_rx_byte_);
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
                    if (machine_.s_tuimode) {
                        tui_->handle_char_write(static_cast<char>(ch));
                    } else {
                        (void)(::write(STDOUT_FILENO, &ch, 1) == 0);
                    }
                    tx_irq_pending_ = true;
                    update_uart_irq(machine_, uart_rx_ready_, uart_ier_, tx_irq_pending_);
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
                    update_uart_irq(machine_, uart_rx_ready_, uart_ier_, tx_irq_pending_);
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

void Uart::tui_update() {
    if (!machine_.s_tuimode) return;

    uint8_t byte = 0;
    while (poll_uart_rx(byte)) {
        if (consume_tui_control_sequence(byte)) {
            continue;
        }

        if (byte == 17 || byte == 3) {  // Ctrl-Q or Ctrl-C
            machine_.is_running_ = false;
            return;
        }
        if (byte == 16) {  // Ctrl-P: Toggle Pause
            tui_pause_loop();
            return;
        }
        if (byte == 9) {  // Tab: Cycle Layout
            if (tui_) {
                tui_->cycle_layout();
            }
            return;
        }
        if (byte == 'r' || byte == 'R') {  // R: Cycle Register Page
            if (tui_) {
                tui_->cycle_reg_page();
            }
            return;
        }
        if (byte == 'u' || byte == 'U') {  // u/U: Scroll Up
            if (tui_) {
                tui_->scroll(5);
            }
            return;
        }
        if (byte == 'd' || byte == 'D') {  // d/D: Scroll Down
            if (tui_) {
                tui_->scroll(-5);
            }
            return;
        }
        if (tui_ && tui_->get_scroll_offset() > 0) {
            if (byte == 'c' || byte == 'C' || byte == '\r' ||
                byte == '\n') {  // c/C/Enter: Reset Scroll to Live
                tui_->reset_scroll();
                return;
            }
        }
        rx_fifo_.push(byte);
        update_uart_irq(machine_, true, uart_ier_, tx_irq_pending_);
    }
}

void Uart::tui_pause_loop() {
    bool paused = true;
    tui_->set_paused(true);
    tui_->render();

    while (paused && machine_.is_running_) {
        if (simrv::tui::g_resized) {
            if (tui_) {
                tui_->render();
            }
        }
        uint8_t byte = 0;
        if (poll_uart_rx(byte)) {
            if (consume_tui_control_sequence(byte)) {
                continue;
            }

            if (byte == 16) {  // Ctrl-P to resume
                paused = false;
            } else if (byte == 'c' || byte == 'C') {  // 'c'/'C': Continue (or reset scroll first if scrolled)
                paused = false;
            } else if (byte == '\r' || byte == '\n') {  // Enter: snap to live first
                if (tui_) {
                    tui_->reset_scroll();
                }
            } else if (byte == 17 || byte == 3 || byte == 'q' ||
                       byte == 'Q') {  // Ctrl-Q/Ctrl-C/q/Q to quit
                machine_.is_running_ = false;
                paused = false;
            } else if (byte == 9) {  // Tab to cycle layout
                if (tui_) {
                    tui_->cycle_layout();
                }
            } else if (byte == 'r' || byte == 'R') {  // 'r'/'R' to cycle register page
                if (tui_) {
                    tui_->cycle_reg_page();
                }
            } else if (byte == 'u' || byte == 'U') {  // u/U: Scroll Up
                if (tui_) {
                    tui_->scroll(5);
                }
            } else if (byte == 'd' || byte == 'D') {  // d/D: Scroll Down
                if (tui_) {
                    tui_->scroll(-5);
                }
            } else if (byte == 's' || byte == 'S' || byte == ' ') {  // 's'/'S'/Space: Step
                uint64_t old_icount = machine_.cpu.e_icount;
                while (machine_.cpu.e_icount == old_icount && machine_.is_running_) {
                    machine_.prepare_cycle();
                    machine_.cpu.run_cycle(machine_);
                    machine_.finalize_cycle();
                }
                tui_->render();
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    tui_->set_paused(false);
    tui_->reset_scroll();
    tui_->render();
}

void Uart::refresh_tui() {
    if (tui_) {
        tui_->render();
    }
}

}  // namespace simrv::device