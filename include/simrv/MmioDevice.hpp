/**
 * @file MmioDevice.hpp
 * @brief SimRV MMIO device interface.
 */
#pragma once

#include "Define.hpp"

class Machine;

class MmioDevice {
   public:
    virtual ~MmioDevice() = default;

    virtual const char* name() const = 0;
    virtual Address base_address() const = 0;
    virtual Address size() const = 0;
    virtual bool read(Machine& machine, Address p_addr, Word& rdata) = 0;
    virtual bool write(Machine& machine, Address p_addr, Word wdata) = 0;

    bool contains(Address addr) const {
        return addr >= base_address() && addr < (base_address() + size());
    }
};