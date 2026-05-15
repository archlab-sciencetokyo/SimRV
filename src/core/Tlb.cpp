/**
 * @file Tlb.cpp
 * @brief Translation Lookaside Buffer (TLB) implementation.
 */
#include "simrv/core/Tlb.hpp"

#include <algorithm>
#include <ranges>

#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/xlen/Constants.hpp"

namespace simrv::core {

void Tlb::flush() {
    inst_r.fill(TLBEntry{});
    data_r.fill(TLBEntry{});
    data_w.fill(TLBEntry{});
}

void Tlb::flush_selective(bool match_all_vaddr, Address vaddr, bool match_all_asid, Word asid) {
    const Address masked_vaddr = vaddr & ~simrv::memory::kPageMask;
    const Word masked_asid = asid & simrv::xlen::kSatpAsidMask;

    auto check_and_flush = [&](TLBEntry& entry) {
        if (!entry.valid) {
            return;
        }
        const bool vaddr_match = match_all_vaddr || (entry.v_addr == masked_vaddr);
        const bool asid_match = match_all_asid || (entry.asid == masked_asid);
        if (vaddr_match && asid_match) {
            entry.valid = false;
        }
    };

    std::ranges::for_each(inst_r, check_and_flush);
    std::ranges::for_each(data_r, check_and_flush);
    std::ranges::for_each(data_w, check_and_flush);
}

}  // namespace simrv::core