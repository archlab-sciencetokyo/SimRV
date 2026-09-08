/**
 * @file Plic.hpp
 * @brief Platform-Level Interrupt Controller (PLIC) MMIO device.
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>

#include "simrv/memory/Mmio.hpp"
#include "simrv/memory/TileLinkNode.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {
class CPU;
struct ArchState;
}  // namespace simrv::core

namespace simrv::device {

/**
 * @class Plic
 * @brief Platform-Level Interrupt Controller (PLIC) front-end and dispatch logic.
 */
class Plic : public memory::TileLinkNode {
   public:
    explicit Plic(core::CPU& cpu) : cpu_(cpu) {}

    static constexpr Address kBaseAddress = simrv::mmio::kPlicBaseAddress;
    static constexpr Address kSize = simrv::mmio::kPlicSize;
    static constexpr size_t kMaxPlicContexts = 32;

    [[nodiscard]] auto name() const -> const char* final { return "plic"; }
    [[nodiscard]] auto base_address() const -> Address final { return kBaseAddress; }
    [[nodiscard]] auto size() const -> Address final { return kSize; }
    void reset() final;
    auto handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool final;

    [[nodiscard]] constexpr auto contains(Address addr) const -> bool {
        return addr >= kBaseAddress && addr < (kBaseAddress + kSize);
    }
    [[nodiscard]] constexpr auto offset(Address addr) const -> Address {
        return addr - kBaseAddress;
    }

    auto mmio_read(Address offset) -> Word;
    void mmio_write(Address offset, Word wdata);

    void update_mip();
    void set_irq(IrqNumber irq_num, int state_val);
    void set_irq(IrqNumber irq_num, bool active) { set_irq(irq_num, active ? 1 : 0); }

    // Pending interrupts (1 bit per IRQ). Index 0 holds IRQs 0-31, etc.
    std::array<Word, 32> plic_pending{};

    // Backing storage for PLIC registers to support OpenSBI drivers
    std::array<InterruptPriority, 1024> plic_priorities{};

    // plic_enables[context][word_idx]. Support up to 32 contexts (16 Harts: M-mode and S-mode).
    std::array<std::array<Word, 32>, kMaxPlicContexts> plic_enables{};
    std::array<InterruptPriority, kMaxPlicContexts> plic_threshold{};
    std::array<InterruptSourceId, kMaxPlicContexts> plic_claim{};

   private:
    core::CPU& cpu_;
    [[nodiscard]] auto get_context_for_offset(Address offset) const -> std::optional<PlicContextId>;
};

}  // namespace simrv::device
