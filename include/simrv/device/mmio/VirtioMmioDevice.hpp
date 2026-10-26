/**
 * @file VirtioMmioDevice.hpp
 * @brief Base class for modern VirtIO-MMIO v2 (VirtIO 1.2) Device Endpoints.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "simrv/device/virtio/IVirtioTransport.hpp"
#include "simrv/device/virtio/VirtioCore.hpp"
#include "simrv/memory/MmioDevice.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::device {

/**
 * @class VirtioMmioDevice
 * @brief Base class for Modern VirtIO-MMIO v2 Devices.
 */
class VirtioMmioDevice : public memory::MmioDevice, public virtio::IVirtioTransport {
   public:
    VirtioMmioDevice(const char* name, simrv::xlen::Address base_address, simrv::xlen::Address size,
                     uint32_t device_id, uint32_t irq_num, core::Machine* machine);
    ~VirtioMmioDevice() override = default;

    [[nodiscard]] auto name() const -> const char* override { return name_; }
    [[nodiscard]] auto base_address() const -> simrv::xlen::Address override {
        return base_address_;
    }
    [[nodiscard]] auto size() const -> simrv::xlen::Address override { return size_; }

    [[nodiscard]] auto read8(simrv::xlen::Address offset) -> uint8_t override;
    [[nodiscard]] auto read16(simrv::xlen::Address offset) -> uint16_t override;
    [[nodiscard]] auto read32(simrv::xlen::Address offset) -> uint32_t override;
    [[nodiscard]] auto read64(simrv::xlen::Address offset) -> uint64_t override;

    void write8(simrv::xlen::Address offset, uint8_t val) override;
    void write16(simrv::xlen::Address offset, uint16_t val) override;
    void write32(simrv::xlen::Address offset, uint32_t val) override;
    void write64(simrv::xlen::Address offset, uint64_t val) override;

    [[nodiscard]] auto device_id() const -> uint32_t { return device_id_; }
    [[nodiscard]] auto device_status() const -> uint32_t { return status_; }
    [[nodiscard]] auto isr_status() const -> uint32_t { return interrupt_status_; }
    [[nodiscard]] auto irq_num() const -> uint32_t { return irq_num_; }
    [[nodiscard]] auto queue_count() const -> std::size_t { return queues_.size(); }
    [[nodiscard]] auto get_queue_state(std::size_t q_idx) const -> const virtio::QueueState* {
        if (q_idx >= queues_.size()) return nullptr;
        return &queues_[q_idx];
    }

    void trigger_irq() override;
    [[nodiscard]] auto is_cycle_mode() const noexcept -> bool override;
    [[nodiscard]] auto is_dma_enabled() const noexcept -> bool override;
    void schedule_deferred_dma(size_t transfer_bytes,
                               std::function<void()> completion_callback) override;

   protected:
    virtual void on_queue_notify(uint32_t q_idx) = 0;
    virtual auto read_device_config(simrv::xlen::Address offset, std::size_t size) -> uint64_t {
        (void)offset;
        (void)size;
        return 0;
    }
    virtual void write_device_config(simrv::xlen::Address offset, uint64_t value,
                                     std::size_t size) {
        (void)offset;
        (void)value;
        (void)size;
    }

    auto dma_read_bytes(uint64_t phys_addr, std::byte* dst, std::size_t len) -> bool override;
    auto dma_write_bytes(uint64_t phys_addr, const std::byte* src, std::size_t len)
        -> bool override;

    void add_queue(uint16_t max_size);

    const char* name_;
    simrv::xlen::Address base_address_;
    simrv::xlen::Address size_;
    uint32_t device_id_;
    uint32_t irq_num_;

    uint32_t status_ = 0;
    uint32_t interrupt_status_ = 0;
    uint32_t device_features_sel_ = 0;
    uint32_t driver_features_sel_ = 0;
    uint32_t driver_features_ = 0;
    uint32_t queue_sel_ = 0;

    std::vector<virtio::QueueState> queues_;
};

}  // namespace simrv::device
