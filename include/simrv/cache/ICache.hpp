/**
 * @file ICache.hpp
 * @brief Level-1 Instruction Cache interface.
 */
#pragma once

#include <concepts>

#include "simrv/CompilerHints.hpp"
#include "simrv/cache/BaseCache.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::cache {

/**
 * @brief Eight-way L1 instruction cache with configurable active geometry.
 */
class ICache : public BaseCache<512, 64, 8> {
   public:
    ICache() = default;

    /// Read an instruction word or halfword at a physical address. Cross-line reads miss.
    [[nodiscard]] auto read(simrv::xlen::PhysAddr addr, uint32_t& data) -> bool {
        return read_value(addr, data);
    }
    [[nodiscard]] auto read16(simrv::xlen::PhysAddr addr, uint16_t& data) -> bool {
        return read_value(addr, data);
    }
    template <std::integral I>
    auto read(I, uint32_t&) = delete;
    template <std::integral I>
    auto read16(I, uint16_t&) = delete;

    auto handle_probe(const simrv::memory::TlChannelB& req, simrv::memory::TlChannelC& resp)
        -> bool;

   private:
    template <typename Value>
        requires(std::same_as<Value, uint16_t> || std::same_as<Value, uint32_t>)
    [[nodiscard]] auto read_value(simrv::xlen::PhysAddr paddr, Value& data) -> bool {
        const auto addr = paddr.raw();
        const uint32_t set_idx = get_set_index(paddr);
        const auto tag = get_tag(paddr);
        last_accessed_set_ = set_idx;

        const auto [w, cache_line] = find_matching_line(set_idx, tag);
        if (simrv::compiler::likely(cache_line != nullptr)) {
            const uint32_t byte_offset = addr & (kLineBytes - 1u);
            if (simrv::compiler::likely(byte_offset + sizeof(Value) <= kLineBytes)) {
                std::memcpy(&data, cache_line->data.data() + byte_offset, sizeof(Value));
                cache_line->last_used = ++access_tick_;
                ++hits_;
                last_access_was_hit_ = true;
                last_hit_way_ = w;
                return true;
            }
        }
        ++misses_;
        last_access_was_hit_ = false;
        last_hit_way_ = 0xFFFFFFFF;
        return false;
    }
};

}  // namespace simrv::cache
