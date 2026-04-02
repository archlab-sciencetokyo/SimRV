/**
 * @file Console.hpp
 * @brief SimRV declarations.
 */
#pragma once
#include "Define.hpp"
#include "MmioDevice.hpp"
#include "State.hpp"
class Machine;
class Console : public MmioDevice {
   public:
    Console();
    static constexpr Address kBaseAddress = static_cast<Address>(0x40000000u);
    static constexpr Address kSize = static_cast<Address>(0x08000000u);

    const char* name() const override { return "console"; }
    Address base_address() const override { return kBaseAddress; }
    Address size() const override { return kSize; }
    bool read(Machine& machine, Address p_addr, Word& rdata) override;
    bool write(Machine& machine, Address p_addr, Word wdata) override;

    Word mmio_read(Address offset);
    void mmio_write(CPU*, Address, Word);
    constexpr bool contains(Address addr) const {
        return addr >= kBaseAddress && addr < (kBaseAddress + kSize);
    }
    constexpr Address offset(Address addr) const { return addr - kBaseAddress; }
    Word console_read(Address offset) { return mmio_read(offset); }
    void console_write(CPU* cpu, Address offset, Word wdata) { mmio_write(cpu, offset, wdata); }
    int recieve_input();
    int MC_recieve_input();

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
