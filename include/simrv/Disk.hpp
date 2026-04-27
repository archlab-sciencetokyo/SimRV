/**
 * @file Disk.hpp
 * @brief Virtio block-disk MMIO device interface.
 */
#pragma once
#include <vector>

#include "Cpu.hpp"
#include "Define.hpp"
#include "MmioDevice.hpp"

class Machine;

/**
 * @class Disk
 * @brief Virtio block device model with queue-backed disk image access.
 */
class Disk : public MmioDevice {
   public:
    /// Construct a disk device instance.
    Disk();
    static constexpr Address kBaseAddress = static_cast<Address>(0x48000000u);
    static constexpr Address kSize = static_cast<Address>(0x08000000u);

    const char* name() const override { return "disk"; }
    Address base_address() const override { return kBaseAddress; }
    Address size() const override { return kSize; }
    /**
     * @brief Read a disk MMIO register.
     * @param machine Active machine instance.
     * @param p_addr Physical MMIO address.
     * @param rdata Output read value.
     * @return true when handled by this device.
     */
    bool read(Machine& machine, Address p_addr, Word& rdata) override;
    /**
     * @brief Write a disk MMIO register.
     * @param machine Active machine instance.
     * @param p_addr Physical MMIO address.
     * @param wdata Value to write.
     * @return true when handled by this device.
     */
    bool write(Machine& machine, Address p_addr, Word wdata) override;

    Word mmio_read(Address) const;
    void mmio_write(Machine& machine, Address offset, Word wdata);
    constexpr bool contains(Address addr) const {
        return addr >= kBaseAddress && addr < (kBaseAddress + kSize);
    }
    constexpr Address offset(Address addr) const { return addr - kBaseAddress; }
    Word disk_read(Address offset) { return mmio_read(offset); }
    void disk_write(Machine& machine, Address offset, Word wdata) {
        mmio_write(machine, offset, wdata);
    }

    Byte* mmem;                             // main memory
    std::vector<Byte> sector_storage_;      // backing owner for disk image storage
    Byte* sector;                           // disk image

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
