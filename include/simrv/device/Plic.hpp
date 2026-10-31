/**
 * @file Plic.hpp
 * @brief Platform-Level Interrupt Controller (PLIC) MMIO device.
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "simrv/core/InterruptTopology.hpp"
#include "simrv/core/PlatformTopology.hpp"
#include "simrv/memory/Mmio.hpp"
#include "simrv/memory/TileLinkNode.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::core {
class CPU;
struct ArchState;
class IInterruptController;
using IInterruptSink = IInterruptController;
}  // namespace simrv::core

namespace simrv::device {

/**
 * @class Plic
 * @brief Platform-Level Interrupt Controller (PLIC) front-end and dispatch logic.
 */
class Plic : public memory::TileLinkNode {
   public:
    explicit Plic(core::CPU& cpu);
    explicit Plic(core::IInterruptController* sink, core::CPU* cpu = nullptr);

    void set_sink(core::IInterruptController* sink) noexcept { sink_ = sink; }

    static constexpr simrv::xlen::Address kBaseAddress = simrv::mmio::kPlicBaseAddress;
    static constexpr simrv::xlen::Address kSize = simrv::mmio::kPlicSize;
    static constexpr uint32_t kNumSources = core::kPlicSourceCount;
    static constexpr size_t kMaxPlicContexts = core::topology::kMaxPlicContexts;

    [[nodiscard]] auto name() const -> const char* final { return "plic"; }
    [[nodiscard]] auto base_address() const -> simrv::xlen::Address final { return base_address_; }
    [[nodiscard]] auto size() const -> simrv::xlen::Address final { return size_; }
    void configure_address_map(simrv::xlen::Address base, simrv::xlen::Address size) noexcept {
        base_address_ = base;
        size_ = size;
    }
    void reset() final;
    auto handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool final;

    [[nodiscard]] auto contains(simrv::xlen::Address addr) const -> bool {
        return addr >= base_address_ && addr - base_address_ < size_;
    }
    [[nodiscard]] auto offset(simrv::xlen::Address addr) const -> simrv::xlen::Address {
        return addr - base_address_;
    }

    auto mmio_read(simrv::xlen::Address offset) -> simrv::xlen::Word;
    void mmio_write(simrv::xlen::Address offset, simrv::xlen::Word wdata);

    void update_mip();
    void set_irq(simrv::xlen::IrqNumber irq_num, int state_val);
    void set_irq(simrv::xlen::IrqNumber irq_num, bool active) { set_irq(irq_num, active ? 1 : 0); }

    // Pending interrupts (1 bit per IRQ). Index 0 holds IRQs 0-31, etc.
    std::array<simrv::xlen::Word, 32> plic_pending{};

    // Backing storage for PLIC registers to support OpenSBI drivers
    std::array<simrv::xlen::InterruptPriority, 1024> plic_priorities{};

    // plic_enables[context][word_idx]. Support up to 32 contexts (16 Harts: M-mode and S-mode).
    std::array<std::array<simrv::xlen::Word, 32>, kMaxPlicContexts> plic_enables{};
    std::array<simrv::xlen::InterruptPriority, kMaxPlicContexts> plic_threshold{};
    std::array<simrv::xlen::InterruptSourceId, kMaxPlicContexts> plic_claim{};

   private:
    core::IInterruptController* sink_{nullptr};
    core::CPU* cpu_{nullptr};
    simrv::xlen::Address base_address_ = kBaseAddress;
    simrv::xlen::Address size_ = kSize;
    [[nodiscard]] auto get_context_for_offset(simrv::xlen::Address offset) const
        -> std::optional<simrv::xlen::PlicContextId>;
};

}  // namespace simrv::device
