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
    PrivilegeLevel priv = PrivilegeLevel::User;
    bool valid{false};
};

/**
 * @class Tlb
 * @brief Manages instruction and data TLB entries.
 */
class Tlb {
   public:
    static constexpr size_t kNumSets = simrv::memory::kTlbSize / 2;

    std::array<std::array<TLBEntry, 2>, kNumSets> inst_r{};
    std::array<std::array<TLBEntry, 2>, kNumSets> data_r{};
    std::array<std::array<TLBEntry, 2>, kNumSets> data_w{};

    std::array<uint8_t, kNumSets> inst_r_lru{};
    std::array<uint8_t, kNumSets> data_r_lru{};
    std::array<uint8_t, kNumSets> data_w_lru{};

    void flush();
    void flush_selective(bool match_all_vaddr, Address vaddr, bool match_all_asid, Word asid);

    [[nodiscard]] inline auto lookup_inst_r(Address vaddr, Word asid, PrivilegeLevel priv) -> TLBEntry* {
        size_t const set = (vaddr >> 12) & (kNumSets - 1);
        Address const vpage = vaddr & ~simrv::memory::kPageMask;
        for (int i = 0; i < 2; i++) {
            auto& entry = inst_r[set][i];
            if (entry.valid && entry.asid == asid && entry.v_addr == vpage && entry.priv == priv) {
                inst_r_lru[set] = 1 - i;
                return &entry;
            }
        }
        return nullptr;
    }

    inline auto insert_inst_r(Address vaddr, Address paddr, Word asid, PrivilegeLevel priv) -> TLBEntry* {
        size_t const set = (vaddr >> 12) & (kNumSets - 1);
        int const way = inst_r_lru[set];
        auto& entry = inst_r[set][way];
        entry.v_addr = vaddr & ~simrv::memory::kPageMask;
        entry.p_addr = paddr & ~simrv::memory::kPageMask;
        entry.asid = asid;
        entry.priv = priv;
        entry.valid = true;
        inst_r_lru[set] = 1 - way;
        return &entry;
    }

    [[nodiscard]] inline auto lookup_data_r(Address vaddr, Word asid, PrivilegeLevel priv) -> TLBEntry* {
        size_t const set = (vaddr >> 12) & (kNumSets - 1);
        Address const vpage = vaddr & ~simrv::memory::kPageMask;
        for (int i = 0; i < 2; i++) {
            auto& entry = data_r[set][i];
            if (entry.valid && entry.asid == asid && entry.v_addr == vpage && entry.priv == priv) {
                data_r_lru[set] = 1 - i;
                return &entry;
            }
        }
        return nullptr;
    }

    inline auto insert_data_r(Address vaddr, Address paddr, Word asid, PrivilegeLevel priv) -> TLBEntry* {
        size_t const set = (vaddr >> 12) & (kNumSets - 1);
        int const way = data_r_lru[set];
        auto& entry = data_r[set][way];
        entry.v_addr = vaddr & ~simrv::memory::kPageMask;
        entry.p_addr = paddr & ~simrv::memory::kPageMask;
        entry.asid = asid;
        entry.priv = priv;
        entry.valid = true;
        data_r_lru[set] = 1 - way;
        return &entry;
    }

    [[nodiscard]] inline auto lookup_data_w(Address vaddr, Word asid, PrivilegeLevel priv) -> TLBEntry* {
        size_t const set = (vaddr >> 12) & (kNumSets - 1);
        Address const vpage = vaddr & ~simrv::memory::kPageMask;
        for (int i = 0; i < 2; i++) {
            auto& entry = data_w[set][i];
            if (entry.valid && entry.asid == asid && entry.v_addr == vpage && entry.priv == priv) {
                data_w_lru[set] = 1 - i;
                return &entry;
            }
        }
        return nullptr;
    }

    inline auto insert_data_w(Address vaddr, Address paddr, Word asid, PrivilegeLevel priv) -> TLBEntry* {
        size_t const set = (vaddr >> 12) & (kNumSets - 1);
        int const way = data_w_lru[set];
        auto& entry = data_w[set][way];
        entry.v_addr = vaddr & ~simrv::memory::kPageMask;
        entry.p_addr = paddr & ~simrv::memory::kPageMask;
        entry.asid = asid;
        entry.priv = priv;
        entry.valid = true;
        data_w_lru[set] = 1 - way;
        return &entry;
    }
};

}  // namespace simrv::core