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

void Tlb::flush_selective(bool match_all_vaddr, Address vaddr, bool match_all_asid, Word asid) {
    if (match_all_vaddr && match_all_asid) {
        flush();
        return;
    }

    if (!match_all_vaddr) {
        const size_t set = calc_set(vaddr);
        const Address vpage = calc_vpage(vaddr);
        for (int i = 0; i < 2; ++i) {
            if (match_all_asid || inst_r[set][i].asid == asid) {
                if (inst_r[set][i].v_addr == vpage) {
                    inst_r[set][i] = TLBEntry{};
                }
            }
            if (match_all_asid || data_r[set][i].asid == asid) {
                if (data_r[set][i].v_addr == vpage) {
                    data_r[set][i] = TLBEntry{};
                }
            }
            if (match_all_asid || data_w[set][i].asid == asid) {
                if (data_w[set][i].v_addr == vpage) {
                    data_w[set][i] = TLBEntry{};
                }
            }
        }
        return;
    }

    if (!match_all_asid) {
        for (size_t s = 0; s < kNumSets; ++s) {
            for (int i = 0; i < 2; ++i) {
                if (inst_r[s][i].asid == asid) inst_r[s][i] = TLBEntry{};
                if (data_r[s][i].asid == asid) data_r[s][i] = TLBEntry{};
                if (data_w[s][i].asid == asid) data_w[s][i] = TLBEntry{};
            }
        }
        return;
    }

    flush();
}

}  // namespace simrv::core