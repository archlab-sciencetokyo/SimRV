/**
 * @file MmioRouter.hpp
 * @brief SimRV MMIO routing helpers.
 */
#pragma once

#include "Define.hpp"

class Machine;
class MmioDevice;

class MmioRouter {
   public:
    explicit MmioRouter(Machine& machine);

    bool read(Address p_addr, Word& rdata);
    bool write(Address p_addr, Word wdata);

   private:
    Machine& machine_;
    // -1: unknown/auto-detect, 0: byte-stride, 2: word-stride (4-byte spacing)
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
