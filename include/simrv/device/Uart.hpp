/**
 * @file Uart.hpp
 * @brief UART device node.
 */
#pragma once

#include <cstdint>

#include "simrv/memory/Mmio.hpp"
#include "simrv/memory/TileLinkNode.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::device {
class Uart : public memory::TileLinkNode {
   public:
    explicit Uart(simrv::core::Machine& machine);
    [[nodiscard]] auto name() const -> const char* override { return "uart"; }
    [[nodiscard]] auto base_address() const -> Address override {
        return simrv::mmio::kUartBaseAddress;
    }
    [[nodiscard]] auto size() const -> Address override { return simrv::mmio::kUartSize; }
    auto handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool override;

   private:
    simrv::core::Machine& machine_;
    int8_t uart_reg_shift_ = -1;
    Word uart_lcr_ = 0;
    Word uart_ier_ = 0;
    Word uart_mcr_ = 0;
    Word uart_scr_ = 0;
    Word uart_dll_ = 0;
    Word uart_dlm_ = 0;
    bool uart_rx_ready_ = false;
    uint8_t uart_rx_byte_ = 0;
};
}  // namespace simrv::device