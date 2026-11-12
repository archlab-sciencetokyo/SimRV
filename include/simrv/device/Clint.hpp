/**
 * @file Clint.hpp
 * @brief Core Local Interruptor (CLINT) MMIO device.
 */
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>

#include "simrv/core/PlatformTopology.hpp"
#include "simrv/memory/Mmio.hpp"
#include "simrv/memory/MmioTypes.hpp"
#include "simrv/memory/TileLinkNode.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::core {
class CPU;
class IInterruptController;
using IInterruptSink = IInterruptController;
}  // namespace simrv::core

namespace simrv::device {

/**
 * @class Clint
 * @brief Core Local Interruptor (CLINT) timer and software-interrupt MMIO front-end.
 */
class Clint : public memory::TileLinkNode {
   public:
    explicit Clint(core::CPU& cpu);
    explicit Clint(core::IInterruptController* sink, core::CPU* cpu = nullptr);

    void set_sink(core::IInterruptController* sink) noexcept { sink_ = sink; }

    static constexpr simrv::xlen::PhysAddr kBaseAddress = simrv::mmio::kClintBaseAddress;
    static constexpr simrv::xlen::Address kSize = simrv::mmio::kClintSize;
    static constexpr size_t kMaxClintHarts = core::topology::kMaxSimulatedHarts;

    [[nodiscard]] auto name() const -> const char* final { return "clint"; }
    [[nodiscard]] auto base_address() const -> simrv::xlen::PhysAddr final { return base_address_; }
    [[nodiscard]] auto size() const -> simrv::xlen::Address final { return size_; }
    void configure_address_map(simrv::xlen::PhysAddr base, simrv::xlen::Address size) noexcept {
        base_address_ = base;
        size_ = size;
    }
    void reset() final;
    auto handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool final;

    using TileLinkNode::contains;
    [[nodiscard]] auto contains(simrv::xlen::PhysAddr address) const -> bool override {
        return address >= base_address_ && address.raw() - base_address_.raw() < size_;
    }

    [[nodiscard]] auto offset(simrv::xlen::PhysAddr addr) const -> simrv::memory::MmioOffset {
        return simrv::memory::MmioOffset{addr.raw() - base_address_.raw()};
    }

    [[nodiscard]] auto mmio_read(simrv::memory::MmioOffset offset) const -> simrv::xlen::Word;
    void mmio_write(simrv::memory::MmioOffset offset, simrv::xlen::Word wdata);

    std::atomic<simrv::xlen::Counter> mtime{1};
    std::atomic<simrv::xlen::Counter> mtimecmp{0};
    std::atomic<bool> supervisor_timer{false};
    std::array<std::atomic<simrv::xlen::Counter>, kMaxClintHarts> hart_mtimecmp{};
    std::array<std::atomic<bool>, kMaxClintHarts> hart_supervisor_timer{};
    simrv::xlen::Counter mcycle{1};
    uint32_t rtc_divider{0};

   private:
    core::IInterruptController* sink_{nullptr};
    core::CPU* cpu_{nullptr};
    simrv::xlen::PhysAddr base_address_ = kBaseAddress;
    simrv::xlen::Address size_ = kSize;
};

}  // namespace simrv::device
