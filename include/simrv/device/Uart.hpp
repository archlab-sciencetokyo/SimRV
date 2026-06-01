/**
 * @file Uart.hpp
 * @brief UART device node.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <queue>
#include <string>

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

    void tui_update();
    void tui_pause_loop();
    void refresh_tui();
    [[nodiscard]] auto tui() -> simrv::tui::Tui* { return tui_.get(); }
    [[nodiscard]] auto tui() const -> const simrv::tui::Tui* { return tui_.get(); }

   private:
    auto consume_tui_control_sequence(uint8_t first_byte) -> bool;
    auto parse_sgr_mouse(const std::string& seq, int& b, int& x, int& y) -> bool;

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
    uint8_t uart_rx_byte_ = 0;

    std::unique_ptr<simrv::tui::Tui> tui_;
    std::queue<uint8_t> rx_fifo_;
    std::string esc_buf_;
};
}  // namespace simrv::device