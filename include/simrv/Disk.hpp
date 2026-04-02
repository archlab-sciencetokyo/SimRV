/**
 * @file Disk.hpp
 * @brief SimRV declarations.
 */
#pragma once
#include "Cpu.hpp"
#include "Define.hpp"
#include "MmioDevice.hpp"

class Machine;
class Disk : public MmioDevice {
   public:
    Disk();
    static constexpr Address kBaseAddress = static_cast<Address>(0x48000000u);
    static constexpr Address kSize = static_cast<Address>(0x08000000u);

    const char* name() const override { return "disk"; }
    Address base_address() const override { return kBaseAddress; }
    Address size() const override { return kSize; }
    bool read(Machine& machine, Address p_addr, Word& rdata) override;
    bool write(Machine& machine, Address p_addr, Word wdata) override;

    Word mmio_read(Address);
    void mmio_write(CPU*, Address, Word);
    constexpr bool contains(Address addr) const {
        return addr >= kBaseAddress && addr < (kBaseAddress + kSize);
    }
    constexpr Address offset(Address addr) const { return addr - kBaseAddress; }
    Word disk_read(Address offset) { return mmio_read(offset); }
    void disk_write(CPU* cpu, Address offset, Word wdata) { mmio_write(cpu, offset, wdata); }

    Byte* mmem;    // main memory
    Byte* sector;  // disk image

    QueueState* Queue; /* Queue of Disk */

    Word DeviceFeaturesSel;
    Word DriverFeatures;
    Word DriverFeaturesSel;
    Word InterruptStatus;
    Word Status;
    Word QueueSel;
    Word QueueNum;
    // struct QueueState Queue[simrv::virtio::kDiskMaxQueueNum];
   private:
};
