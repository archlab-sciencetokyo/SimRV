/**
 * @file MmioDevice.hpp
 * @brief SimRV MMIO device interface.
 */
#pragma once

#include "XLen.hpp"
class Machine;

/**
 * @class MmioDevice
 * @brief Abstract interface for memory-mapped I/O devices.
 */
class MmioDevice {
   public:
    virtual ~MmioDevice() = default;

    /// Return short device name used for logs/debug output.
    virtual const char* name() const = 0;
    /// Return base physical address of the MMIO region.
    virtual Address base_address() const = 0;
    /// Return region size in bytes.
    virtual Address size() const = 0;
    /// Read device register at physical address.
    virtual bool read(Machine& machine, Address p_addr, Word& rdata) = 0;
    /// Write device register at physical address.
    virtual bool write(Machine& machine, Address p_addr, Word wdata) = 0;

    bool contains(Address addr) const {
        return addr >= base_address() && addr < (base_address() + size());
    }
};