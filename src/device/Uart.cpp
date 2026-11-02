/**
 * @file Uart.cpp
 * @brief Deterministic 16550-compatible serial device.
 */
#include "simrv/device/Uart.hpp"

#include <sys/select.h>
#include <unistd.h>

#include <cerrno>

#include "simrv/core/Machine.hpp"
#include "simrv/device/UartTiming.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/util/FormatUtil.hpp"

namespace simrv::device {
namespace {
constexpr uint8_t kIirNone = 0x01;
constexpr uint8_t kIirTx = 0x02;
constexpr uint8_t kIirRx = 0x04;
constexpr uint8_t kIirLine = 0x06;
constexpr uint8_t kLsrDataReady = 0x01;
constexpr uint8_t kLsrOverrun = 0x02;
constexpr uint8_t kLsrThre = 0x20;
constexpr uint8_t kLsrTemt = 0x40;
constexpr std::size_t kFifoCapacity = 16;
constexpr std::size_t kPendingHostInputCapacity = 2048;

auto fifo_capacity(bool enabled) -> std::size_t { return enabled ? kFifoCapacity : 1; }
auto divisor(uint8_t dll, uint8_t dlm) -> uint32_t {
    const uint32_t value = static_cast<uint32_t>(dll) | (static_cast<uint32_t>(dlm) << 8U);
    return value == 0 ? 1 : value;
}
auto frame_half_bits(uint8_t lcr) -> uint32_t {
    const uint32_t data = 5U + (lcr & 0x03U);
    const uint32_t parity = (lcr & 0x08U) != 0 ? 1U : 0U;
    const uint32_t stop_half_bits = (lcr & 0x04U) != 0 ? ((data == 5U) ? 3U : 4U) : 2U;
    return 2U + 2U * (data + parity) + stop_half_bits;
}
}  // namespace

Uart::Uart(simrv::core::Machine& machine, simrv::xlen::Address base, simrv::xlen::Address size,
           uint32_t irq, uint64_t clock_hz, uint64_t cpu_clock_hz)
    : machine_(machine),
      base_(base),
      size_(size),
      irq_(irq),
      clock_hz_(clock_hz != 0 ? clock_hz : SIMRV_PLATFORM_UART_CLOCK_HZ),
      cpu_clock_hz_(cpu_clock_hz != 0 ? cpu_clock_hz : simrv::core::timing::kCpuClockHz) {
    const auto initial = uart_reset_divisor(clock_hz_);
    uart_dll_ = static_cast<uint8_t>(initial & 0xffU);
    uart_dlm_ = static_cast<uint8_t>(initial >> 8U);
}

Uart::~Uart() {
    flush_tx_buffer();
    stop_input_thread();
    stop_pty();
}

void Uart::start_input_thread() {
    if (machine_.tui_enabled()) return;
    if (::isatty(STDIN_FILENO) && !simrv::util::owns_interactive_terminal()) return;
    input_thread_stop_.store(false, std::memory_order_relaxed);
    input_thread_ = std::thread([this] {
        while (!input_thread_stop_.load(std::memory_order_relaxed)) {
            fd_set read_fds;
            timeval timeout{0, 20000};
            FD_ZERO(&read_fds);
            FD_SET(STDIN_FILENO, &read_fds);
            if (::select(STDIN_FILENO + 1, &read_fds, nullptr, nullptr, &timeout) <= 0) continue;
            uint8_t byte = 0;
            const auto count = ::read(STDIN_FILENO, &byte, 1);
            if (count == 0) return;
            if (count != 1) {
                if (errno != EINTR) return;
                continue;
            }
            if (byte == 17) {
                machine_.is_running_ = false;
                return;
            }
            push_rx_byte(byte);
        }
    });
}

void Uart::stop_input_thread() {
    input_thread_stop_.store(true, std::memory_order_relaxed);
    if (input_thread_.joinable()) input_thread_.join();
}

auto Uart::start_pty() -> bool {
    if (pty_.is_open()) return true;
    if (!pty_.open()) return false;
    pty_reader_stop_.store(false, std::memory_order_relaxed);
    pty_reader_thread_ = std::thread([this] {
        uint8_t buffer[256];
        while (!pty_reader_stop_.load(std::memory_order_relaxed)) {
            fd_set fds;
            FD_ZERO(&fds);
            const int fd = pty_.master_fd();
            if (fd < 0) break;
            FD_SET(fd, &fds);
            timeval timeout{0, 20000};
            if (::select(fd + 1, &fds, nullptr, nullptr, &timeout) <= 0) continue;
            const auto count = pty_.read_from_master(buffer, sizeof(buffer));
            for (ssize_t i = 0; i < count; ++i) push_rx_byte(buffer[i]);
        }
    });
    return true;
}

void Uart::flush_tx_buffer() noexcept {
    if (tx_buffer_len_ == 0) return;
    if (pty_.is_open())
        (void)pty_.write_to_master(tx_buffer_.data(), tx_buffer_len_);
    else if (!machine_.console_sink())
        (void)::write(STDOUT_FILENO, tx_buffer_.data(), tx_buffer_len_);
    tx_buffer_len_ = 0;
}

void Uart::stop_pty() {
    flush_tx_buffer();
    pty_reader_stop_.store(true, std::memory_order_relaxed);
    if (pty_reader_thread_.joinable()) pty_reader_thread_.join();
    pty_.close();
}

void Uart::advance_cycles(uint64_t cycles) {
    if (cycles == 0) return;
    const uint64_t frame_ticks = static_cast<uint64_t>(frame_half_bits(uart_lcr_)) * 8ULL *
                                 divisor(uart_dll_, uart_dlm_) * cpu_clock_hz_;
    {
        std::scoped_lock lock(rx_mutex_);
        if (!rx_shift_active_ && !pending_rx_.empty()) {
            rx_shift_byte_ = pending_rx_.front();
            pending_rx_.pop_front();
            rx_shift_active_ = true;
            rx_phase_ = 0;
        }
    }
    if (tx_shift_active_) tx_phase_ += cycles * clock_hz_;
    if (rx_shift_active_) rx_phase_ += cycles * clock_hz_;
    bool tx_completed = false;
    while (tx_phase_ >= frame_ticks) {
        tx_phase_ -= frame_ticks;
        if (!tx_shift_active_) {
            if (tx_fifo_.empty()) break;
            tx_shift_byte_ = tx_fifo_.front();
            tx_fifo_.pop_front();
            tx_shift_active_ = true;
            tx_phase_ = 0;
            if (tx_fifo_.empty()) tx_irq_pending_ = true;
            continue;
        }
        const auto ch = tx_shift_byte_;
        if (machine_.console_sink())
            machine_.console_sink()->handle_char_write(static_cast<char>(ch));
        if (pty_.is_open() || !machine_.console_sink()) {
            tx_buffer_[tx_buffer_len_++] = ch;
            if (ch == '\n' || tx_buffer_len_ == tx_buffer_.size()) flush_tx_buffer();
        }
        tx_shift_active_ = false;
        if (tx_fifo_.empty()) tx_irq_pending_ = true;
        tx_completed = true;
        if (!tx_fifo_.empty()) {
            tx_shift_byte_ = tx_fifo_.front();
            tx_fifo_.pop_front();
            tx_shift_active_ = true;
            if (tx_fifo_.empty()) tx_irq_pending_ = true;
        }
    }

    bool rx_changed = false;
    while (true) {
        {
            std::scoped_lock lock(rx_mutex_);
            if (!rx_shift_active_ && !pending_rx_.empty()) {
                rx_shift_byte_ = pending_rx_.front();
                pending_rx_.pop_front();
                rx_shift_active_ = true;
            }
        }
        if (!rx_shift_active_ || rx_phase_ < frame_ticks) break;
        rx_phase_ -= frame_ticks;
        rx_shift_active_ = false;
        {
            std::scoped_lock lock(rx_mutex_);
            if (rx_fifo_.size() >= fifo_capacity(fcr_fifo_enabled_))
                rx_overrun_ = true;
            else {
                rx_fifo_.push_back(rx_shift_byte_);
                rx_changed = true;
            }
        }
    }
    if (tx_completed || rx_changed) irq_update_pending_.store(true, std::memory_order_release);
    service_interrupts();
}

auto Uart::handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool {
    const auto reg = simrv::mmio::uart_reg(req.address.raw(), uart_reg_shift_, base_);
    if (reg == simrv::mmio::kUartRegInvalid) {
        if (req.opcode == memory::TlOpcodeA::Get) resp.data = 0;
        return true;
    }
    const bool dlab = (uart_lcr_ & simrv::mmio::kUartLcrDlabMask) != 0;
    if (req.opcode == memory::TlOpcodeA::Get) {
        switch (reg) {
            case simrv::mmio::kUartRegRbrThrDll:
                if (dlab)
                    resp.data = uart_dll_;
                else {
                    std::scoped_lock lock(rx_mutex_);
                    resp.data = rx_fifo_.empty() ? 0 : rx_fifo_.front();
                    if (!rx_fifo_.empty()) rx_fifo_.pop_front();
                    irq_update_pending_.store(true, std::memory_order_release);
                }
                break;
            case simrv::mmio::kUartRegIerDlm:
                resp.data = dlab ? uart_dlm_ : uart_ier_;
                break;
            case simrv::mmio::kUartRegIirFcr: {
                uint8_t iir = kIirNone;
                const uint8_t lsr = static_cast<uint8_t>((rx_overrun_ ? kLsrOverrun : 0) |
                                                         (!rx_fifo_.empty() ? kLsrDataReady : 0));
                if ((uart_ier_ & 0x04U) && (lsr & kLsrOverrun))
                    iir = kIirLine;
                else if ((uart_ier_ & 0x01U) && rx_fifo_.size() >= fcr_trigger_level_)
                    iir = kIirRx;
                else if ((uart_ier_ & 0x02U) && tx_irq_pending_) {
                    iir = kIirTx;
                    tx_irq_pending_ = false;
                    irq_update_pending_.store(true, std::memory_order_release);
                }
                if (fcr_fifo_enabled_) iir |= 0xc0U;
                resp.data = iir;
                break;
            }
            case simrv::mmio::kUartRegLcr:
                resp.data = uart_lcr_;
                break;
            case simrv::mmio::kUartRegMcr:
                resp.data = uart_mcr_;
                break;
            case simrv::mmio::kUartRegLsr: {
                std::scoped_lock lock(rx_mutex_);
                resp.data = (rx_fifo_.empty() ? 0 : kLsrDataReady) |
                            (rx_overrun_ ? kLsrOverrun : 0) | (tx_fifo_.empty() ? kLsrThre : 0) |
                            (tx_fifo_.empty() && !tx_shift_active_ ? kLsrTemt : 0);
                rx_overrun_ = false;
                irq_update_pending_.store(true, std::memory_order_release);
                break;
            }
            case simrv::mmio::kUartRegMsr:
                resp.data = 0xb0;
                break;
            case simrv::mmio::kUartRegScr:
                resp.data = uart_scr_;
                break;
            default:
                resp.data = 0;
        }
    } else {
        const uint8_t value = static_cast<uint8_t>(req.data & 0xffU);
        switch (reg) {
            case simrv::mmio::kUartRegRbrThrDll:
                if (dlab)
                    uart_dll_ = value;
                else {
                    const auto capacity = fifo_capacity(fcr_fifo_enabled_);
                    if (!tx_shift_active_) {
                        tx_shift_active_ = true;
                        tx_shift_byte_ = value;
                        tx_phase_ = 0;
                    } else if (tx_fifo_.size() < capacity) {
                        tx_fifo_.push_back(value);
                    }
                    tx_irq_pending_ = tx_fifo_.empty();
                }
                break;
            case simrv::mmio::kUartRegIerDlm:
                if (dlab)
                    uart_dlm_ = value;
                else {
                    uart_ier_ = value & 0x0fU;
                    if ((uart_ier_ & 2U) && tx_fifo_.empty()) tx_irq_pending_ = true;
                }
                break;
            case simrv::mmio::kUartRegIirFcr: {
                fcr_fifo_enabled_ = (value & 1U) != 0;
                constexpr uint8_t kTriggerLevels[] = {1, 4, 8, 14};
                fcr_trigger_level_ = fcr_fifo_enabled_ ? kTriggerLevels[(value >> 6U) & 3U] : 1;
                if ((value & 2U) != 0) {
                    std::scoped_lock lock(rx_mutex_);
                    rx_fifo_.clear();
                    rx_overrun_ = false;
                }
                if ((value & 4U) != 0) {
                    tx_fifo_.clear();
                    tx_shift_active_ = false;
                }
                if (!fcr_fifo_enabled_) {
                    while (rx_fifo_.size() > 1) rx_fifo_.pop_back();
                    while (tx_fifo_.size() > 1) tx_fifo_.pop_back();
                }
                break;
            }
            case simrv::mmio::kUartRegLcr:
                uart_lcr_ = value;
                break;
            case simrv::mmio::kUartRegMcr:
                uart_mcr_ = value & 0x1fU;
                break;
            case simrv::mmio::kUartRegScr:
                uart_scr_ = value;
                break;
            default:
                break;
        }
        irq_update_pending_.store(true, std::memory_order_release);
    }
    service_interrupts();
    return true;
}

void Uart::push_rx_byte(uint8_t byte) {
    std::scoped_lock lock(rx_mutex_);
    if (pending_rx_.size() < kPendingHostInputCapacity) pending_rx_.push_back(byte);
}

void Uart::service_interrupts() {
    if (!irq_update_pending_.exchange(false, std::memory_order_acq_rel)) return;
    std::scoped_lock lock(rx_mutex_);
    const bool line_irq = (uart_ier_ & 4U) && rx_overrun_;
    const bool rx_irq = (uart_ier_ & 1U) && rx_fifo_.size() >= fcr_trigger_level_;
    const bool tx_irq = (uart_ier_ & 2U) && tx_irq_pending_;
    machine_.set_platform_irq(static_cast<int>(irq_), line_irq || rx_irq || tx_irq);
}

auto Uart::is_interrupt_pending() const -> bool {
    std::scoped_lock lock(rx_mutex_);
    return ((uart_ier_ & 4U) && rx_overrun_) ||
           ((uart_ier_ & 1U) && rx_fifo_.size() >= fcr_trigger_level_) ||
           ((uart_ier_ & 2U) && tx_irq_pending_);
}
}  // namespace simrv::device
