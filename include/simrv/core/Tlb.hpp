/**
 * @file Tlb.hpp
 * @brief Translation Lookaside Buffer (TLB) encapsulation.
 */
#pragma once

#include <array>
#include <utility>

#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

/**
 * @struct TLBEntry
 * @brief Represents a single 2-way set-associative TLB entry.
 *
 * Cache-line aligned to 32 bytes with explicit padding for zero-cost offset calculations.
 */
struct alignas(32) TLBEntry {
    Address v_addr{};                            ///< Virtual page address (8 bytes)
    Address p_addr{};                            ///< Physical page address (8 bytes)
    Word asid{};                                 ///< Address Space Identifier (8 bytes)
    PrivilegeLevel priv = PrivilegeLevel::User;  ///< Architectural privilege level (1 byte)
    bool valid{false};                           ///< Entry validity flag (1 byte)
    std::array<uint8_t, 6> padding{};            ///< 6-byte padding (32 bytes total size)
};

/**
 * @class Tlb
 * @brief 2-way set-associative Translation Lookaside Buffer for fast virtual-to-physical address
 * translation.
 *
 * Separate cache-line-aligned arrays are maintained for instruction fetch (inst_r),
 * data reads (data_r), and data writes (data_w) with 1-bit LRU replacement policy tracking.
 */
class Tlb {
   public:
    /// Number of sets in each 2-way set-associative TLB table
    static constexpr size_t kNumSets = simrv::memory::kTlbSize / 2;

    /// 2-way associative set type
    using TLBSet = std::array<TLBEntry, 2>;

    alignas(64) std::array<TLBSet, kNumSets> inst_r{};  ///< Instruction read TLB table
    alignas(64) std::array<TLBSet, kNumSets> data_r{};  ///< Data read TLB table
    alignas(64) std::array<TLBSet, kNumSets> data_w{};  ///< Data write TLB table

    alignas(64) std::array<uint8_t, kNumSets> inst_r_lru{};  ///< LRU bit per set for inst_r
    alignas(64) std::array<uint8_t, kNumSets> data_r_lru{};  ///< LRU bit per set for data_r
    alignas(64) std::array<uint8_t, kNumSets> data_w_lru{};  ///< LRU bit per set for data_w

    /**
     * @brief Compute the TLB set index for a given virtual address.
     * @param vaddr Virtual address.
     * @return Set index in range [0, kNumSets - 1].
     */
    [[nodiscard]] static constexpr inline auto calc_set(Address vaddr) noexcept -> size_t {
        return (vaddr >> 12) & (kNumSets - 1);
    }

    /**
     * @brief Mask off page offset to obtain page base address.
     * @param vaddr Virtual address.
     * @return Aligned page base address.
     */
    [[nodiscard]] static constexpr inline auto calc_vpage(Address vaddr) noexcept -> Address {
        return vaddr & ~simrv::memory::kPageMask;
    }

    /**
     * @brief Invalidate all entries across instruction and data TLBs.
     */
    void flush();

    /**
     * @brief Selectively invalidate entries matching specified virtual address and ASID
     * constraints.
     * @param match_all_vaddr If true, ignores vaddr match requirement.
     * @param vaddr Target virtual address.
     * @param match_all_asid If true, ignores ASID match requirement.
     * @param asid Target ASID.
     */
    void flush_selective(bool match_all_vaddr, Address vaddr, bool match_all_asid, Word asid);

    /**
     * @brief Lookup instruction fetch TLB entry for virtual address, ASID, and privilege level.
     * @param vaddr Virtual address.
     * @param asid Address Space Identifier.
     * @param priv Privilege level.
     * @return Pointer to matching TLBEntry if hit, or nullptr on miss.
     */
    [[nodiscard]] inline auto lookup_inst_r(Address vaddr, Word asid, PrivilegeLevel priv)
        -> TLBEntry* {
        const size_t set = calc_set(vaddr);
        const Address vpage = calc_vpage(vaddr);
        for (int i = 0; i < 2; i++) {
            auto& entry = inst_r[set][i];
            if (entry.valid && entry.asid == asid && entry.v_addr == vpage && entry.priv == priv) {
                inst_r_lru[set] = 1 - i;
                return &entry;
            }
        }
        return nullptr;
    }

    /**
     * @brief Insert entry into instruction fetch TLB using LRU replacement.
     * @param vaddr Virtual address.
     * @param paddr Physical address.
     * @param asid Address Space Identifier.
     * @param priv Privilege level.
     * @return Pointer to newly inserted TLBEntry.
     */
    inline auto insert_inst_r(Address vaddr, Address paddr, Word asid, PrivilegeLevel priv)
        -> TLBEntry* {
        const size_t set = calc_set(vaddr);
        const int way = inst_r_lru[set];
        auto& entry = inst_r[set][way];
        entry.v_addr = calc_vpage(vaddr);
        entry.p_addr = paddr & ~simrv::memory::kPageMask;
        entry.asid = asid;
        entry.priv = priv;
        entry.valid = true;
        inst_r_lru[set] = 1 - way;
        return &entry;
    }

    /**
     * @brief Lookup data read TLB entry for virtual address, ASID, and privilege level.
     * @param vaddr Virtual address.
     * @param asid Address Space Identifier.
     * @param priv Privilege level.
     * @return Pointer to matching TLBEntry if hit, or nullptr on miss.
     */
    [[nodiscard]] inline auto lookup_data_r(Address vaddr, Word asid, PrivilegeLevel priv)
        -> TLBEntry* {
        const size_t set = calc_set(vaddr);
        const Address vpage = calc_vpage(vaddr);
        for (int i = 0; i < 2; i++) {
            auto& entry = data_r[set][i];
            if (entry.valid && entry.asid == asid && entry.v_addr == vpage && entry.priv == priv) {
                data_r_lru[set] = 1 - i;
                return &entry;
            }
        }
        return nullptr;
    }

    /**
     * @brief Insert entry into data read TLB using LRU replacement.
     * @param vaddr Virtual address.
     * @param paddr Physical address.
     * @param asid Address Space Identifier.
     * @param priv Privilege level.
     * @return Pointer to newly inserted TLBEntry.
     */
    inline auto insert_data_r(Address vaddr, Address paddr, Word asid, PrivilegeLevel priv)
        -> TLBEntry* {
        const size_t set = calc_set(vaddr);
        const int way = data_r_lru[set];
        auto& entry = data_r[set][way];
        entry.v_addr = calc_vpage(vaddr);
        entry.p_addr = paddr & ~simrv::memory::kPageMask;
        entry.asid = asid;
        entry.priv = priv;
        entry.valid = true;
        data_r_lru[set] = 1 - way;
        return &entry;
    }

    /**
     * @brief Lookup data write TLB entry for virtual address, ASID, and privilege level.
     * @param vaddr Virtual address.
     * @param asid Address Space Identifier.
     * @param priv Privilege level.
     * @return Pointer to matching TLBEntry if hit, or nullptr on miss.
     */
    [[nodiscard]] inline auto lookup_data_w(Address vaddr, Word asid, PrivilegeLevel priv)
        -> TLBEntry* {
        const size_t set = calc_set(vaddr);
        const Address vpage = calc_vpage(vaddr);
        for (int i = 0; i < 2; i++) {
            auto& entry = data_w[set][i];
            if (entry.valid && entry.asid == asid && entry.v_addr == vpage && entry.priv == priv) {
                data_w_lru[set] = 1 - i;
                return &entry;
            }
        }
        return nullptr;
    }

    /**
     * @brief Insert entry into data write TLB using LRU replacement.
     * @param vaddr Virtual address.
     * @param paddr Physical address.
     * @param asid Address Space Identifier.
     * @param priv Privilege level.
     * @return Pointer to newly inserted TLBEntry.
     */
    inline auto insert_data_w(Address vaddr, Address paddr, Word asid, PrivilegeLevel priv)
        -> TLBEntry* {
        const size_t set = calc_set(vaddr);
        const int way = data_w_lru[set];
        auto& entry = data_w[set][way];
        entry.v_addr = calc_vpage(vaddr);
        entry.p_addr = paddr & ~simrv::memory::kPageMask;
        entry.asid = asid;
        entry.priv = priv;
        entry.valid = true;
        data_w_lru[set] = 1 - way;
        return &entry;
    }
};

}  // namespace simrv::core