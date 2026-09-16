/**
 * @file Tlb.cpp
 * @brief Translation Lookaside Buffer (TLB) implementation.
 */
#include "simrv/core/Tlb.hpp"

#include <algorithm>

#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/xlen/Constants.hpp"

namespace simrv::core {

void Tlb::flush() {
    for (auto& set : inst_r) set.fill(TLBEntry{});
    for (auto& set : data_r) set.fill(TLBEntry{});
    for (auto& set : data_w) set.fill(TLBEntry{});
    inst_r_lru.fill(0);
    data_r_lru.fill(0);
    data_w_lru.fill(0);
}

void Tlb::flush_selective(const TlbFlushFilter& filter) {
    if (!filter.vaddr && !filter.asid) {
        flush();
        return;
    }

    if (filter.vaddr) {
        const auto set = calc_set(*filter.vaddr);
        const Address vpage = calc_vpage(*filter.vaddr);
        auto clear_match = [&](TLBEntry& entry) {
            if (entry.valid && entry.v_addr == vpage &&
                (!filter.asid || entry.asid == *filter.asid)) {
                entry = TLBEntry{};
            }
        };
        for (int i = 0; i < 2; ++i) {
            clear_match(inst_r[set][i]);
            clear_match(data_r[set][i]);
            clear_match(data_w[set][i]);
        }
        return;
    }

    if (filter.asid) {
        for (size_t s = 0; s < kNumSets; ++s) {
            for (int i = 0; i < 2; ++i) {
                if (inst_r[s][i].valid && inst_r[s][i].asid == *filter.asid)
                    inst_r[s][i] = TLBEntry{};
                if (data_r[s][i].valid && data_r[s][i].asid == *filter.asid)
                    data_r[s][i] = TLBEntry{};
                if (data_w[s][i].valid && data_w[s][i].asid == *filter.asid)
                    data_w[s][i] = TLBEntry{};
            }
        }
        return;
    }

    flush();
}

}  // namespace simrv::core
