/**
 * @file DCache.hpp
 * @brief Level-1 Data Cache interface.
 */
#pragma once

#include <array>
#include <cstdint>

#include "simrv/xlen/Types.hpp"

namespace simrv::cache {

class DCache {
   public:
    static constexpr uint32_t kNumLines = 64;
    static constexpr uint32_t kLineBytes = 32;
    static constexpr uint32_t kLineShift = 5;
    static constexpr uint32_t kWays = 4;
    static constexpr uint32_t kNumSets = kNumLines / kWays;
    static constexpr uint32_t kSetMask = kNumSets - 1;

    DCache() = default;

    [[nodiscard]] auto read(Address addr, Word& data, Instruction funct3) -> bool;
    void write(Address addr, Word data, Instruction funct3);
    void insert(Address base_addr, const Byte* line_data);
    void flush();

    [[nodiscard]] auto hit_count() const -> uint64_t { return hits_; }
    [[nodiscard]] auto miss_count() const -> uint64_t { return misses_; }

   private:
    struct CacheLine {
        Address tag = ~Address{0};
        std::array<Byte, kLineBytes> data{};
        uint64_t last_used = 0;
        bool valid = false;
    };

    std::array<std::array<CacheLine, kWays>, kNumSets> sets_{};
    uint64_t access_tick_ = 0;
    uint64_t hits_ = 0;
    uint64_t misses_ = 0;

    [[nodiscard]] auto get_set_index(Address addr) const -> uint32_t {
        return (addr >> kLineShift) & kSetMask;
    }
    [[nodiscard]] auto get_tag(Address addr) const -> Address {
        return addr & ~static_cast<Address>((1u << kLineShift) - 1u);
    }
};

}  // namespace simrv::cache