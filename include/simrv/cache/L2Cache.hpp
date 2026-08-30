/**
 * @file L2Cache.hpp
 * @brief Level-2 Unified Cache interface.
 */
#pragma once

#include "simrv/cache/BaseCache.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::cache {

/**
 * @class L2Cache
 * @brief Intermediate unified Level-2 Cache (default 64 KiB, 32-byte lines, 8 ways).
 */
class L2Cache : public BaseCache<2048, 32, 8> {
   public:
    L2Cache() = default;

    [[nodiscard]] auto read_line(Address line_base, std::array<Byte, kLineBytes>& out_data)
        -> bool {
        const uint32_t set_idx = get_set_index(line_base);
        const Address tag = get_tag(line_base);
        last_accessed_set_ = set_idx;

        const auto way_opt = find_way(set_idx, tag);
        if (way_opt.has_value()) {
            const uint32_t w = *way_opt;
            auto& line = sets_[set_idx][w];
            std::memcpy(out_data.data(), line.data.data(), kLineBytes);
            line.last_used = ++access_tick_;
            ++hits_;
            last_access_was_hit_ = true;
            last_hit_way_ = w;
            return true;
        }

        ++misses_;
        last_access_was_hit_ = false;
        last_hit_way_ = 0xFFFFFFFF;
        return false;
    }

    void write_line(Address line_base, const std::array<Byte, kLineBytes>& in_data,
                    simrv::memory::MesiState state = simrv::memory::MesiState::Exclusive) {
        insert(line_base, in_data.data(), state);
    }
};

}  // namespace simrv::cache
