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
#include "simrv/tui/TuiKey.hpp"

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
    if (seq.size() < 7 || seq.at(0) != '\x1b' || seq.at(1) != '[' || seq.at(2) != '<') {
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
    while (static_cast<int>(esc_buf_.size()) < kMaxSeqLen) {
        bool polled = false;
        for (int retry = 0; retry < 5; ++retry) {
            if (poll_uart_rx(byte)) {
                polled = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!polled) {
            break;
        }
        esc_buf_.push_back(static_cast<char>(byte));
        if (byte == 'M' || byte == 'm' || byte == '~' ||
            (byte >= 'A' && byte <= 'Z' && byte != 'O') ||
            (byte >= 'a' && byte <= 'z')) {
            break;
        }
    }

    // 1. Mouse reporting
    int button = 0;
    int x = 0;
    int y = 0;
    if (parse_sgr_mouse(esc_buf_, button, x, y)) {
        // Double-click prevention: only register left clicks on press event ('M')
        if (esc_buf_.back() == 'M' && button == 0 && y == 2) {
            const auto layout = tui_->get_layout();
            bool on_left = false;
            if (layout == simrv::tui::TuiLayout::Split) {
                on_left = (x <= tui_->get_pane_width());
            } else if (layout == simrv::tui::TuiLayout::FullRegister) {
                on_left = true;
            } else {
                on_left = false;
            }

            if (on_left) {
                tui_->cycle_reg_page();
            } else {
                if (machine_.is_shutdown_) {
                    machine_.request_reboot();
                    tui_loop_paused_ = false;
                } else {
                    if (tui_loop_paused_) {
                        tui_loop_paused_ = false;
                    } else {
                        tui_pause_loop();
                    }
                }
            }
            return true;
        }

        tui_->handle_mouse(x, y, button);
        return true;
    }

    // 2. Alt modifier shortcuts
    if (esc_buf_.size() == 2) {
        char key = esc_buf_.at(1);
        if (key == 'p' || key == 'P') {
            tui_->cycle_right_panel_mode();
            return true;
        }
        if (key == 'r' || key == 'R') {
            tui_->cycle_reg_page();
            return true;
        }
        if (key == 'h' || key == 'H') {
            tui_->toggle_high_contrast();
            return true;
        }
        if (key == 't' || key == 'T') {
            tui_->toggle_sakura_theme();
            return true;
        }
        if (key == 'l' || key == 'L') {
            tui_->cycle_layout();
            return true;
        }
        if (key == 'u' || key == 'U') {
            tui_->scroll(5);
            return true;
        }
        if (key == 'd' || key == 'D') {
            tui_->scroll(-5);
            return true;
        }
        if (key == 'w' || key == 'W') {
            tui_->scroll_regs(-2);
            return true;
        }
        if (key == 's' || key == 'S') {
            tui_->scroll_regs(2);
            return true;
        }
        if (key == 'z' || key == 'Z') {
            tui_->reset_scroll_regs();
            return true;
        }
        if (key == 'c' || key == 'C') {
            tui_->reset_scroll();
            return true;
        }
    }

    // 3. Forward all other escape sequences (like arrow keys) to the guest OS
    for (char c : esc_buf_) {
        rx_fifo_.push(static_cast<uint8_t>(c));
    }
    update_uart_irq(machine_, true, uart_ier_, tx_irq_pending_);
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
                        if (!uart_rx_ready_ && !rx_fifo_.empty()) {
                            uart_rx_byte_ = rx_fifo_.front();
                            rx_fifo_.pop();
                            uart_rx_ready_ = true;
                        }
                        if (!uart_rx_ready_) {
                            uart_rx_ready_ = poll_uart_rx(uart_rx_byte_);
                            if (uart_rx_ready_ && uart_rx_byte_ == 17) {
                                machine_.is_running_ = false;
                            }
                            update_uart_irq(machine_, uart_rx_ready_, uart_ier_, tx_irq_pending_);
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
            case simrv::mmio::kUartRegIirFcr:
                {
                    const bool rx_ready = machine_.s_tuimode ? !rx_fifo_.empty() : uart_rx_ready_;
                    if (rx_ready) {
                        resp.data = static_cast<Word>(0x04U);
                    } else if (tx_irq_pending_ && ((uart_ier_ & static_cast<Word>(0x2U)) != 0)) {
                        resp.data = static_cast<Word>(0x02U);
                        tx_irq_pending_ = false;
                        update_uart_irq(machine_, rx_ready, uart_ier_, tx_irq_pending_);
                    } else {
                        resp.data = static_cast<Word>(0x01U);
                    }
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
                    if (!uart_rx_ready_ && !rx_fifo_.empty()) {
                        uart_rx_byte_ = rx_fifo_.front();
                        rx_fifo_.pop();
                        uart_rx_ready_ = true;
                        update_uart_irq(machine_, uart_rx_ready_, uart_ier_, tx_irq_pending_);
                    }
                    if (!uart_rx_ready_) {
                        uart_rx_ready_ = poll_uart_rx(uart_rx_byte_);
                        if (uart_rx_ready_ && uart_rx_byte_ == 17) {
                            machine_.is_running_ = false;
                        }
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
                    const bool rx_ready = machine_.s_tuimode ? !rx_fifo_.empty() : uart_rx_ready_;
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
                    const bool rx_ready = machine_.s_tuimode ? !rx_fifo_.empty() : uart_rx_ready_;
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

void Uart::tui_update() {
    if (!machine_.s_tuimode) return;

    uint8_t byte = 0;
    while (poll_uart_rx(byte)) {
        if (consume_tui_control_sequence(byte)) {
            continue;
        }

        const auto key = static_cast<simrv::tui::TuiKey>(byte);
        if (key == simrv::tui::TuiKey::CtrlQ) {
            machine_.is_running_ = false;
            return;
        }
        if (key == simrv::tui::TuiKey::CtrlP) {
            tui_pause_loop();
            return;
        }
        rx_fifo_.push(byte);
        update_uart_irq(machine_, true, uart_ier_, tx_irq_pending_);
    }
}

void Uart::tui_pause_loop() {
    tui_loop_paused_ = true;
    tui_->set_paused(true);
    tui_->render();

    while (tui_loop_paused_ && (machine_.is_running_ || machine_.is_shutdown_)) {
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

            const auto key = static_cast<simrv::tui::TuiKey>(byte);
            if (key == simrv::tui::TuiKey::CtrlP || key == simrv::tui::TuiKey::c || key == simrv::tui::TuiKey::C) {
                if (!machine_.is_shutdown_) {
                    tui_loop_paused_ = false;
                }
            } else if (key == simrv::tui::TuiKey::CtrlR) {
                machine_.request_reboot();
                tui_loop_paused_ = false;
            } else if (key == simrv::tui::TuiKey::Enter || key == simrv::tui::TuiKey::Newline) {
                if (tui_) {
                    tui_->reset_scroll();
                }
            } else if (key == simrv::tui::TuiKey::CtrlQ || key == simrv::tui::TuiKey::CtrlC ||
                       key == simrv::tui::TuiKey::q || key == simrv::tui::TuiKey::Q) {
                machine_.is_running_ = false;
                machine_.is_shutdown_ = false;
                tui_loop_paused_ = false;
            } else if (key == simrv::tui::TuiKey::Tab) {
                if (tui_) {
                    tui_->cycle_layout();
                }
            } else if (key == simrv::tui::TuiKey::r || key == simrv::tui::TuiKey::R) {
                if (tui_) {
                    tui_->cycle_reg_page();
                }
            } else if (key == simrv::tui::TuiKey::e || key == simrv::tui::TuiKey::E) {
                if (tui_) {
                    tui_->toggle_explain();
                }
            } else if (key == simrv::tui::TuiKey::LeftBracket) {
                if (tui_) {
                    tui_->adjust_left_pane_width(-2);
                }
            } else if (key == simrv::tui::TuiKey::RightBracket) {
                if (tui_) {
                    tui_->adjust_left_pane_width(2);
                }
            } else if (key == simrv::tui::TuiKey::h || key == simrv::tui::TuiKey::H) {
                if (tui_) {
                    tui_->toggle_high_contrast();
                }
            } else if (key == simrv::tui::TuiKey::t || key == simrv::tui::TuiKey::T) {
                if (tui_) {
                    tui_->toggle_sakura_theme();
                }
            } else if (key == simrv::tui::TuiKey::p || key == simrv::tui::TuiKey::P) {
                if (tui_) {
                    tui_->cycle_right_panel_mode();
                }
            } else if (key == simrv::tui::TuiKey::v || key == simrv::tui::TuiKey::V) {
                if (tui_) {
                    tui_->toggle_trace_enabled();
                }
            } else if (key == simrv::tui::TuiKey::u || key == simrv::tui::TuiKey::U) {
                if (tui_) {
                    tui_->scroll(5);
                }
            } else if (key == simrv::tui::TuiKey::d || key == simrv::tui::TuiKey::D) {
                if (tui_) {
                    tui_->scroll(-5);
                }
            } else if (key == simrv::tui::TuiKey::s || key == simrv::tui::TuiKey::S || key == simrv::tui::TuiKey::Space) {
                if (!machine_.is_shutdown_) {
                    if (tui_) {
                        tui_->update_cache();
                    }
                    uint64_t old_icount = machine_.cpu.e_icount;
                    while (machine_.cpu.e_icount == old_icount && machine_.is_running_) {
                        machine_.prepare_cycle();
                        machine_.cpu.run_cycle(machine_);
                        machine_.finalize_cycle();
                    }
                    tui_->render();
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (tui_) {
        tui_->update_cache();
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

void Uart::non_tui_poll_input() {
    if (machine_.s_tuimode) return;

    if (uart_rx_ready_ && uart_rx_byte_ == 17) {
        machine_.is_running_ = false;
        return;
    }

    uint8_t byte = 0;
    if (poll_uart_rx(byte)) {
        if (byte == 17) {  // Ctrl-Q
            machine_.is_running_ = false;
            return;
        }

        rx_fifo_.push(byte);
        if (!uart_rx_ready_) {
            uart_rx_byte_ = rx_fifo_.front();
            rx_fifo_.pop();
            uart_rx_ready_ = true;
            update_uart_irq(machine_, true, uart_ier_, tx_irq_pending_);
        }
    }
}

}  // namespace simrv::device