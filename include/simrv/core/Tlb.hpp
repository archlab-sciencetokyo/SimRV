/**
 * @file Tlb.hpp
 * @brief Translation Lookaside Buffer (TLB) encapsulation.
 */
#pragma once
#include <array>
#include <optional>
#include <utility>

#include "simrv/CompilerHints.hpp"
#include "simrv/core/TlbTypes.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/memory/PageTableWalker.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::core {

/**
 * @struct TLBEntry
 * @brief Represents a single 2-way set-associative TLB entry.
 *
 * Cache-line aligned to 32 bytes for zero-cost offset calculations.
 */
struct alignas(32) TLBEntry {
    simrv::xlen::Address v_addr{};  ///< Virtual page address
    simrv::xlen::Address p_addr{};  ///< Physical page address
    Asid asid{};                    ///< Address Space Identifier (2 bytes)
    simrv::xlen::PrivilegeLevel priv =
        simrv::xlen::PrivilegeLevel::User;  ///< Architectural privilege level (1 byte)
    simrv::memory::PtePbmt pbmt = simrv::memory::PtePbmt::Pma;  ///< Translated memory type
    bool valid{false};                      ///< Entry validity flag (1 byte)
    bool global{false};                     ///< Global page mapping flag (1 byte)
};

/**
 * @struct TlbFlushFilter
 * @brief Specifies selective flush criteria, replacing boolean-flag parameters.
 *
 * If vaddr is nullopt, all virtual addresses match (global flush).
 * If asid is nullopt, all ASIDs match.
 */
struct TlbFlushFilter {
    std::optional<simrv::xlen::Address> vaddr;
    std::optional<Asid> asid;
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
     * @brief Compute the TLB set index for a given virtual address with bit-mixed hashing.
     * @param vaddr Virtual address.
     * @return Set index in range [0, kNumSets - 1].
     */
    [[nodiscard]] static constexpr inline auto calc_set(simrv::xlen::Address vaddr) noexcept
        -> TlbSetIndex {
        const simrv::xlen::Address vpn = vaddr >> 12;
        return static_cast<TlbSetIndex>((vpn ^ (vpn >> 8)) & (kNumSets - 1));
    }

    /**
     * @brief Mask off page offset to obtain page base address.
     * @param vaddr Virtual address.
     * @return Aligned page base address.
     */
    [[nodiscard]] static constexpr inline auto calc_vpage(simrv::xlen::Address vaddr) noexcept
        -> simrv::xlen::Address {
        return vaddr & ~simrv::memory::kPageMask;
    }

    /**
     * @brief Invalidate all entries across instruction and data TLBs.
     */
    void flush();

    /**
     * @brief Invalidate all entries in data read and write TLBs, preserving instruction TLB.
     */
    void flush_data();

    /**
     * @brief Selectively invalidate entries matching the filter criteria.
     * @param filter Flush filter specifying optional vaddr and ASID constraints.
     */
    void flush_selective(const TlbFlushFilter& filter);

    /**
     * @brief Fast templated TLB lookup by access kind.
     * @tparam Kind Access kind (Instruction, DataRead, DataWrite).
     * @param vaddr Virtual address.
     * @param asid Address Space Identifier.
     * @param priv Privilege level.
     * @return Pointer to matching TLBEntry if hit, or nullptr on miss.
     */
    template <TlbAccessKind Kind>
    [[nodiscard]] inline auto lookup(simrv::xlen::Address vaddr, Asid asid,
                                     simrv::xlen::PrivilegeLevel priv) -> TLBEntry* {
        auto& table = select_table_static<Kind>();
        auto& lru = select_lru_static<Kind>();
        const auto set = calc_set(vaddr);
        const simrv::xlen::Address vpage = calc_vpage(vaddr);
        auto& set_entries = table[set];
        if (simrv::compiler::likely(set_entries[0].v_addr == vpage && set_entries[0].valid &&
                                    (set_entries[0].global || set_entries[0].asid == asid) &&
                                    set_entries[0].priv == priv)) {
            lru[set] = 1;
            return &set_entries[0];
        }
        if (set_entries[1].v_addr == vpage && set_entries[1].valid &&
            (set_entries[1].global || set_entries[1].asid == asid) && set_entries[1].priv == priv) {
            lru[set] = 0;
            return &set_entries[1];
        }
        return nullptr;
    }

    /**
     * @brief Fast templated TLB insert by access kind using LRU replacement.
     * @tparam Kind Access kind (Instruction, DataRead, DataWrite).
     * @param vaddr Virtual address.
     * @param paddr Physical address.
     * @param asid Address Space Identifier.
     * @param priv Privilege level.
     * @param global Global page flag.
     * @return Pointer to newly inserted TLBEntry.
     */
    template <TlbAccessKind Kind>
    inline auto insert(simrv::xlen::Address vaddr, simrv::xlen::Address paddr, Asid asid,
                       simrv::xlen::PrivilegeLevel priv, bool global = false,
                       simrv::memory::PtePbmt pbmt = simrv::memory::PtePbmt::Pma) -> TLBEntry* {
        auto& table = select_table_static<Kind>();
        auto& lru = select_lru_static<Kind>();
        const auto set = calc_set(vaddr);
        const simrv::xlen::Address vpage = calc_vpage(vaddr);
        const simrv::xlen::Address ppage = paddr & ~simrv::memory::kPageMask;
        auto& set_entries = table[set];

        if (set_entries[0].v_addr == vpage && set_entries[0].valid &&
            (global ? set_entries[0].global
                    : (!set_entries[0].global && set_entries[0].asid == asid)) &&
            set_entries[0].priv == priv) {
            set_entries[0].p_addr = ppage;
            set_entries[0].global = global;
            set_entries[0].pbmt = pbmt;
            lru[set] = 1;
            return &set_entries[0];
        }
        if (set_entries[1].v_addr == vpage && set_entries[1].valid &&
            (global ? set_entries[1].global
                    : (!set_entries[1].global && set_entries[1].asid == asid)) &&
            set_entries[1].priv == priv) {
            set_entries[1].p_addr = ppage;
            set_entries[1].global = global;
            set_entries[1].pbmt = pbmt;
            lru[set] = 0;
            return &set_entries[1];
        }

        const int way = !set_entries[0].valid ? 0 : (!set_entries[1].valid ? 1 : lru[set]);
        auto& entry = set_entries[way];
        entry.v_addr = vpage;
        entry.p_addr = ppage;
        entry.asid = asid;
        entry.priv = priv;
        entry.global = global;
        entry.pbmt = pbmt;
        entry.valid = true;
        lru[set] = 1 - way;
        return &entry;
    }

    /// Inspect a translation without updating replacement state or hit counters.
    template <TlbAccessKind Kind>
    [[nodiscard]] constexpr auto peek(simrv::xlen::Address vaddr, Asid asid,
                                      simrv::xlen::PrivilegeLevel priv) const noexcept
        -> const TLBEntry* {
        const auto& table = select_table_static<Kind>();
        const auto set = calc_set(vaddr);
        const simrv::xlen::Address vpage = calc_vpage(vaddr);
        for (const auto& entry : table[set]) {
            if (entry.v_addr == vpage && entry.valid && (entry.global || entry.asid == asid) &&
                entry.priv == priv) {
                return &entry;
            }
        }
        return nullptr;
    }

    /// Inspect an instruction translation without updating replacement state or hit counters.
    [[nodiscard]] constexpr auto peek_inst_r(simrv::xlen::Address vaddr, Asid asid,
                                             simrv::xlen::PrivilegeLevel priv) const noexcept
        -> const TLBEntry* {
        return peek<TlbAccessKind::Instruction>(vaddr, asid, priv);
    }

    /// Inspect a data read translation without updating replacement state or hit counters.
    [[nodiscard]] constexpr auto peek_data_r(simrv::xlen::Address vaddr, Asid asid,
                                             simrv::xlen::PrivilegeLevel priv) const noexcept
        -> const TLBEntry* {
        return peek<TlbAccessKind::DataRead>(vaddr, asid, priv);
    }

    /// Inspect a data write translation without updating replacement state or hit counters.
    [[nodiscard]] constexpr auto peek_data_w(simrv::xlen::Address vaddr, Asid asid,
                                             simrv::xlen::PrivilegeLevel priv) const noexcept
        -> const TLBEntry* {
        return peek<TlbAccessKind::DataWrite>(vaddr, asid, priv);
    }

    // --- Direct forwarding wrappers (templated, zero-cost) ---

    [[nodiscard]] inline auto lookup_inst_r(simrv::xlen::Address vaddr, Asid asid,
                                            simrv::xlen::PrivilegeLevel priv) -> TLBEntry* {
        return lookup<TlbAccessKind::Instruction>(vaddr, asid, priv);
    }

    inline auto insert_inst_r(simrv::xlen::Address vaddr, simrv::xlen::Address paddr, Asid asid,
                              simrv::xlen::PrivilegeLevel priv, bool global = false,
                              simrv::memory::PtePbmt pbmt = simrv::memory::PtePbmt::Pma)
        -> TLBEntry* {
        return insert<TlbAccessKind::Instruction>(vaddr, paddr, asid, priv, global, pbmt);
    }

    [[nodiscard]] inline auto lookup_data_r(simrv::xlen::Address vaddr, Asid asid,
                                            simrv::xlen::PrivilegeLevel priv) -> TLBEntry* {
        return lookup<TlbAccessKind::DataRead>(vaddr, asid, priv);
    }

    inline auto insert_data_r(simrv::xlen::Address vaddr, simrv::xlen::Address paddr, Asid asid,
                              simrv::xlen::PrivilegeLevel priv, bool global = false,
                              simrv::memory::PtePbmt pbmt = simrv::memory::PtePbmt::Pma)
        -> TLBEntry* {
        return insert<TlbAccessKind::DataRead>(vaddr, paddr, asid, priv, global, pbmt);
    }

    [[nodiscard]] inline auto lookup_data_w(simrv::xlen::Address vaddr, Asid asid,
                                            simrv::xlen::PrivilegeLevel priv) -> TLBEntry* {
        return lookup<TlbAccessKind::DataWrite>(vaddr, asid, priv);
    }

    inline auto insert_data_w(simrv::xlen::Address vaddr, simrv::xlen::Address paddr, Asid asid,
                              simrv::xlen::PrivilegeLevel priv, bool global = false,
                              simrv::memory::PtePbmt pbmt = simrv::memory::PtePbmt::Pma)
        -> TLBEntry* {
        return insert<TlbAccessKind::DataWrite>(vaddr, paddr, asid, priv, global, pbmt);
    }

   private:
    template <TlbAccessKind Kind>
    [[nodiscard]] constexpr auto select_table_static() noexcept -> std::array<TLBSet, kNumSets>& {
        if constexpr (Kind == TlbAccessKind::Instruction) {
            return inst_r;
        } else if constexpr (Kind == TlbAccessKind::DataRead) {
            return data_r;
        } else {
            return data_w;
        }
    }

    template <TlbAccessKind Kind>
    [[nodiscard]] constexpr auto select_table_static() const noexcept
        -> const std::array<TLBSet, kNumSets>& {
        if constexpr (Kind == TlbAccessKind::Instruction) {
            return inst_r;
        } else if constexpr (Kind == TlbAccessKind::DataRead) {
            return data_r;
        } else {
            return data_w;
        }
    }

    template <TlbAccessKind Kind>
    [[nodiscard]] constexpr auto select_lru_static() noexcept -> std::array<uint8_t, kNumSets>& {
        if constexpr (Kind == TlbAccessKind::Instruction) {
            return inst_r_lru;
        } else if constexpr (Kind == TlbAccessKind::DataRead) {
            return data_r_lru;
        } else {
            return data_w_lru;
        }
    }
};

}  // namespace simrv::core
