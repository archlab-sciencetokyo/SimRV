/**
 * @file Console.hpp
 * @brief Virtio console MMIO device interface.
 */
#pragma once
#include "Cpu.hpp"
#include "Define.hpp"
#include "MmioDevice.hpp"

class Machine;

/**
 * @class Console
 * @brief Virtio-console device model wired to the MMIO router.
 *
 * This device exposes queue state and data registers used by guest software
 * for character input/output.
 */
class Console : public MmioDevice {
   public:
    /// Construct a console device instance.
    Console();
    static constexpr Address kBaseAddress = static_cast<Address>(0x40000000u);
    static constexpr Address kSize = static_cast<Address>(0x08000000u);

    [[nodiscard]] auto name() const -> const char* override { return "console"; }
    [[nodiscard]] auto base_address() const -> Address override { return kBaseAddress; }
    [[nodiscard]] auto size() const -> Address override { return kSize; }
    /**
     * @brief Read an MMIO register.
     * @param machine Active machine instance.
     * @param p_addr Physical MMIO address.
     * @param rdata Output read value.
     * @return true if the address was handled by this device.
     */
    auto read(Machine& machine, Address p_addr, Word& rdata) -> bool override;
    /**
     * @brief Write an MMIO register.
     * @param machine Active machine instance.
     * @param p_addr Physical MMIO address.
     * @param wdata Value to write.
     * @return true if the address was handled by this device.
     */
    auto write(Machine& machine, Address p_addr, Word wdata) -> bool override;

    [[nodiscard]] auto mmio_read(Address offset) const -> Word;
    void mmio_write(Machine& machine, Address offset, Word wdata);
    [[nodiscard]] constexpr auto contains(Address addr) const -> bool {
        return addr >= kBaseAddress && addr < (kBaseAddress + kSize);
    }
    constexpr Address offset(Address addr) const { return addr - kBaseAddress; }
    Word console_read(Address offset) { return mmio_read(offset); }
    void console_write(Machine& machine, Address offset, Word wdata) {
        mmio_write(machine, offset, wdata);
    }
    int receive_input() const;
    int MC_receive_input(Machine& machine);

    Byte* mmem;  // main memory

    QueueState* Queue; /* Queue of Console */

    Word DeviceFeaturesSel;
    Word DriverFeatures;
    Word DriverFeaturesSel;
    Word InterruptStatus;
    Word Status;
    Word QueueSel;
    Word QueueNum;

    Byte cons_fifo;
    Byte fifo_en;

    // struct QueueState Queue[simrv::virtio::kConsoleMaxQueueNum];
   private:
};
