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

    [[nodiscard]] auto name() const -> const char* override { return "disk"; }
    [[nodiscard]] auto base_address() const -> Address override { return kBaseAddress; }
    [[nodiscard]] auto size() const -> Address override { return kSize; }
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
    auto write(Machine& machine, Address p_addr, Word wdata) -> bool override;

    [[nodiscard]] auto mmio_read(Address) const -> Word;
    void mmio_write(Machine& machine, Address offset, Word wdata);
    [[nodiscard]] constexpr auto contains(Address addr) const -> bool {
        return addr >= kBaseAddress && addr < (kBaseAddress + kSize);
    }
    [[nodiscard]] constexpr auto offset(Address addr) const -> Address {
        return addr - kBaseAddress;
    }
    auto disk_read(Address offset) -> Word { return mmio_read(offset); }
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
