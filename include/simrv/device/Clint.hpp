/**
 * @file Clint.hpp
 * @brief Core Local Interruptor (CLINT) MMIO device.
 */
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>

#include "simrv/memory/Mmio.hpp"
#include "simrv/memory/TileLinkNode.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {
class CPU;
}  // namespace simrv::core

namespace simrv::device {

/**
 * @class Clint
 * @brief Core Local Interruptor (CLINT) timer and software-interrupt MMIO front-end.
 */
class Clint : public memory::TileLinkNode {
   public:
    explicit Clint(core::CPU& cpu);

    static constexpr Address kBaseAddress = simrv::mmio::kClintBaseAddress;
    static constexpr Address kSize = simrv::mmio::kClintSize;
    static constexpr size_t kMaxClintHarts = 16;

    [[nodiscard]] auto name() const -> const char* final { return "clint"; }
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

    [[nodiscard]] auto mmio_read(Address offset) const -> Word;
    void mmio_write(Address offset, Word wdata);

    std::atomic<Counter> mtime{1};
    std::atomic<Counter> mtimecmp{0};
    std::atomic<bool> supervisor_timer{false};
    std::array<std::atomic<Counter>, kMaxClintHarts> hart_mtimecmp{};
    std::array<std::atomic<bool>, kMaxClintHarts> hart_supervisor_timer{};
    Counter mcycle{1};
    int rtc_divider{0};

   private:
    core::CPU& cpu_;
};

}  // namespace simrv::device
