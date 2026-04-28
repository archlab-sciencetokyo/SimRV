/**
 * @file MmioRouter.hpp
 * @brief MMIO request dispatch router.
 */
#pragma once

#include "XLen.hpp"
class Machine;
class MmioDevice;

/**
 * @class MmioRouter
 * @brief Dispatches MMIO reads/writes to concrete device handlers.
 */
class MmioRouter {
   public:
    /// Construct router bound to simulator machine state.
    explicit MmioRouter(Machine& machine);

    /**
     * @brief Route MMIO read request.
     * @param p_addr Physical MMIO address.
     * @param rdata Output value when read is handled.
     * @return true if handled by a routed device.
     */
    auto read(Address p_addr, Word& rdata) -> bool;
    /**
     * @brief Route MMIO write request.
     * @param p_addr Physical MMIO address.
     * @param wdata Value to write.
     * @return true if handled by a routed device.
     */
    auto write(Address p_addr, Word wdata) -> bool;

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
