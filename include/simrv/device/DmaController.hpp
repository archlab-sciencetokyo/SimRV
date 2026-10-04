/**
 * @file DmaController.hpp
 * @brief Programmable interrupt-based MMIO DMA Controller.
 */
#pragma once

#include <cstddef>
#include <cstdint>

#include "simrv/memory/Mmio.hpp"
#include "simrv/memory/MmioDevice.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::device {

/**
 * @class DmaController
 * @brief Memory-mapped Direct Memory Access (DMA) engine with completion interrupts
 *        and cycle-accurate latency modeling.
 *
 * Register Map (32-bit aligned):
 *  - 0x00: CONTROL
 *            Bit 0: START (write 1 to trigger transfer, self-clearing)
 *            Bit 1: INT_ENABLE (1 = assert IRQ on completion or error)
 *            Bit 2: BUSY (read-only: 1 while transfer is active, 0 when idle)
 *  - 0x04: STATUS
 *            Bit 0: DONE (1 = transfer completed successfully)
 *            Bit 1: ERROR (1 = transfer failed, e.g., invalid address or zero length)
 *  - 0x08: SRC_ADDR_LO (lower 32 bits of source physical address)
 *  - 0x0C: SRC_ADDR_HI (upper 32 bits of source physical address)
 *  - 0x10: DST_ADDR_LO (lower 32 bits of destination physical address)
 *  - 0x14: DST_ADDR_HI (upper 32 bits of destination physical address)
 *  - 0x18: BYTE_COUNT  (number of bytes to copy)
 *  - 0x1C: INTERRUPT_ACK / STATUS_CLEAR (write 1 to clear DONE/ERROR and deassert IRQ)
 */
class DmaController : public memory::MmioDevice {
   public:
    static constexpr Address kDefaultBaseAddress = mmio::kDmaControllerBaseAddress;
    static constexpr Address kDefaultSize = mmio::kDmaControllerSize;
    static constexpr uint32_t kDefaultIrq = mmio::kDmaControllerIrq;

    // Register Offsets
    static constexpr Address kRegControl = 0x00U;
    static constexpr Address kRegStatus = 0x04U;
    static constexpr Address kRegSrcAddrLo = 0x08U;
    static constexpr Address kRegSrcAddrHi = 0x0CU;
    static constexpr Address kRegDstAddrLo = 0x10U;
    static constexpr Address kRegDstAddrHi = 0x14U;
    static constexpr Address kRegByteCount = 0x18U;
    static constexpr Address kRegInterruptAck = 0x1CU;

    // Control Bitmasks
    static constexpr uint32_t kControlStart = 1U << 0;
    static constexpr uint32_t kControlIntEnable = 1U << 1;
    static constexpr uint32_t kControlBusy = 1U << 2;

    // Status Bitmasks
    static constexpr uint32_t kStatusDone = 1U << 0;
    static constexpr uint32_t kStatusError = 1U << 1;

    explicit DmaController(core::Machine* machine = nullptr,
                           Address base_address = kDefaultBaseAddress, Address size = kDefaultSize,
                           uint32_t irq_num = kDefaultIrq);
    ~DmaController() override = default;

    [[nodiscard]] auto name() const -> const char* override { return "dma-controller"; }
    [[nodiscard]] auto base_address() const -> Address override { return base_address_; }
    [[nodiscard]] auto size() const -> Address override { return size_; }

    [[nodiscard]] auto read32(Address offset) -> uint32_t override;
    void write32(Address offset, uint32_t val) override;

    [[nodiscard]] auto is_busy() const noexcept -> bool { return busy_; }
    [[nodiscard]] auto is_done() const noexcept -> bool { return (status_ & kStatusDone) != 0; }
    [[nodiscard]] auto is_error() const noexcept -> bool { return (status_ & kStatusError) != 0; }
    [[nodiscard]] auto irq_num() const noexcept -> uint32_t { return irq_num_; }

    [[nodiscard]] auto src_address() const noexcept -> uint64_t {
        return static_cast<uint64_t>(src_addr_lo_) | (static_cast<uint64_t>(src_addr_hi_) << 32);
    }
    [[nodiscard]] auto dst_address() const noexcept -> uint64_t {
        return static_cast<uint64_t>(dst_addr_lo_) | (static_cast<uint64_t>(dst_addr_hi_) << 32);
    }
    [[nodiscard]] auto byte_count() const noexcept -> uint32_t { return byte_count_; }

    void reset() noexcept override;

   private:
    void start_transfer();
    void execute_transfer(uint64_t src, uint64_t dst, uint32_t count);

    Address base_address_;
    Address size_;
    uint32_t irq_num_;

    uint32_t control_{0};
    uint32_t status_{0};
    uint32_t src_addr_lo_{0};
    uint32_t src_addr_hi_{0};
    uint32_t dst_addr_lo_{0};
    uint32_t dst_addr_hi_{0};
    uint32_t byte_count_{0};
    bool busy_{false};
};

}  // namespace simrv::device
