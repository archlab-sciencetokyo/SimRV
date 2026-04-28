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
    [[nodiscard]] virtual auto name() const -> const char* = 0;
    /// Return base physical address of the MMIO region.
    [[nodiscard]] virtual auto base_address() const -> Address = 0;
    /// Return region size in bytes.
    [[nodiscard]] virtual auto size() const -> Address = 0;
    /// Read device register at physical address.
    virtual auto read(Machine& machine, Address p_addr, Word& rdata) -> bool = 0;
    /// Write device register at physical address.
    virtual auto write(Machine& machine, Address p_addr, Word wdata) -> bool = 0;

    [[nodiscard]] auto contains(Address addr) const -> bool {
        return addr >= base_address() && addr < (base_address() + size());
    }
};