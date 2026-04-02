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
};
