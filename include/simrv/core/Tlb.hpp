/**
 * @file Tlb.hpp
 * @brief Translation Lookaside Buffer (TLB) encapsulation.
 */
#pragma once

#include <array>

#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

struct TLBEntry {
    Address v_addr{};
    Address p_addr{};
    Word asid{};
    bool valid{false};
};

/**
 * @class Tlb
 * @brief Manages instruction and data TLB entries.
 */
class Tlb {
   public:
    std::array<TLBEntry, simrv::memory::kTlbSize> inst_r{};
    std::array<TLBEntry, simrv::memory::kTlbSize> data_r{};
    std::array<TLBEntry, simrv::memory::kTlbSize> data_w{};

    void flush();
    void flush_selective(bool match_all_vaddr, Address vaddr, bool match_all_asid, Word asid);
};

}  // namespace simrv::core