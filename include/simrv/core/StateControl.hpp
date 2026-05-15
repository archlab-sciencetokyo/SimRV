/**
 * @file StateControl.hpp
 * @brief SimRV state control helper interfaces.
 */
#pragma once

#include "simrv/memory/Mmio.hpp"
#include "simrv/memory/TileLinkNode.hpp"

namespace simrv::core {

class CPU;
class Tlb;
struct ArchState;

class PlicMmio;

/**
 * @class InterruptController
 * @brief Encapsulates interrupt pending/enable updates for CPU state.
 */
class InterruptController {
   public:
    static void updateMip(PlicMmio& plic, ArchState& state);
    static void setIrq(PlicMmio& plic, ArchState& state, int irq_num, int state_val);
};

/**
 * @class PlicMmio
 * @brief Platform-level interrupt controller MMIO front-end.
 */
class PlicMmio : public memory::TileLinkNode {
   public:
    explicit PlicMmio(CPU& cpu) : cpu_(cpu) {}

    static constexpr Address kBaseAddress = simrv::mmio::kPlicBaseAddress;
    static constexpr Address kSize = simrv::mmio::kPlicSize;

    [[nodiscard]] auto name() const -> const char* final { return "plic"; }
    [[nodiscard]] auto base_address() const -> Address final { return kBaseAddress; }
    [[nodiscard]] auto size() const -> Address final { return kSize; }
    auto handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool final;

    [[nodiscard]] constexpr auto contains(Address addr) const -> bool {
        return addr >= kBaseAddress && addr < (kBaseAddress + kSize);
    }
    [[nodiscard]] constexpr auto offset(Address addr) const -> Address {
        return addr - kBaseAddress;
    }

    auto mmio_read(Address offset) -> Word;
    void mmio_write(Address offset, Word wdata);

    CSRValue pending_irq{};
    CSRValue served_irq{};

   private:
    CPU& cpu_;
};

/**
 * @class ClintMmio
 * @brief CLINT timer/software-interrupt MMIO front-end.
 */
class ClintMmio : public memory::TileLinkNode {
   public:
    explicit ClintMmio(CPU& cpu) : cpu_(cpu) {}

    static constexpr Address kBaseAddress = simrv::mmio::kClintBaseAddress;
    static constexpr Address kSize = simrv::mmio::kClintSize;

    [[nodiscard]] auto name() const -> const char* final { return "clint"; }
    [[nodiscard]] auto base_address() const -> Address final { return kBaseAddress; }
    [[nodiscard]] auto size() const -> Address final { return kSize; }
    auto handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool final;

    [[nodiscard]] constexpr auto contains(Address addr) const -> bool {
        return addr >= kBaseAddress && addr < (kBaseAddress + kSize);
    }
    [[nodiscard]] constexpr auto offset(Address addr) const -> Address {
        return addr - kBaseAddress;
    }

    [[nodiscard]] auto mmio_read(Address offset) const -> Word;
    void mmio_write(Address offset, Word wdata);

    Counter mtime{1};
    Counter mtimecmp{};

   private:
    CPU& cpu_;
};

/**
 * @class TrapController
 * @brief Handles trap entry/return sequencing and cause propagation.
 */
class TrapController {
   public:
    static void mret(ArchState& state, Tlb& tlb);
    static void sret(ArchState& state, Tlb& tlb);
    static void raiseException(CPU& cpu, TrapCause cause, CSRValue tval);
};

}  // namespace simrv::core
