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
    inst_r.fill(TLBEntry{});
    data_r.fill(TLBEntry{});
    data_w.fill(TLBEntry{});
}

void Tlb::flush_selective(bool /*match_all_vaddr*/, Address /*vaddr*/, bool /*match_all_asid*/, Word /*asid*/) {
    // Stale translations for superpages (HugeTLB) can cause kernel crashes if we selectively
    // flush only a single 4KB subpage, since a superpage spans multiple subpages.
    // To ensure correctness and safety under all virtual memory configurations,
    // we perform a full TLB invalidation (which is a standard-compliant simplification).
    flush();
}

}  // namespace simrv::core