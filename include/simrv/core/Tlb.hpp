/**
 * @file Tlb.hpp
 * @brief Translation Lookaside Buffer (TLB) encapsulation.
 */
#pragma once

#include <array>
#include <optional>
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
    Address v_addr{};                            ///< Virtual page address
    Address p_addr{};                            ///< Physical page address
    Asid asid{};                                 ///< Address Space Identifier (2 bytes)
    PrivilegeLevel priv = PrivilegeLevel::User;  ///< Architectural privilege level (1 byte)
    bool valid{false};                           ///< Entry validity flag (1 byte)
    // Padding to reach 32-byte alignment. Size depends on Word width.
    static constexpr size_t kPadding =
        32 - 2 * sizeof(Address) - sizeof(Asid) - sizeof(PrivilegeLevel) - sizeof(bool);
    std::array<uint8_t, kPadding> padding{};
};

/**
 * @struct TlbFlushFilter
 * @brief Specifies selective flush criteria, replacing boolean-flag parameters.
 *
 * If vaddr is nullopt, all virtual addresses match (global flush).
 * If asid is nullopt, all ASIDs match.
 */
struct TlbFlushFilter {
    std::optional<Address> vaddr;
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
    [[nodiscard]] static constexpr inline auto calc_set(Address vaddr) noexcept -> TlbSetIndex {
        const Address vpn = vaddr >> 12;
        return static_cast<TlbSetIndex>((vpn ^ (vpn >> 8)) & (kNumSets - 1));
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
    [[nodiscard]] inline auto lookup(Address vaddr, Asid asid, PrivilegeLevel priv) -> TLBEntry* {
        auto& table = select_table_static<Kind>();
        auto& lru = select_lru_static<Kind>();
        const auto set = calc_set(vaddr);
        const Address vpage = calc_vpage(vaddr);
        auto& set_entries = table[set];
        if (simrv::compiler::likely(set_entries[0].valid && set_entries[0].v_addr == vpage &&
                                    set_entries[0].asid == asid && set_entries[0].priv == priv)) {
            lru[set] = 1;
            return &set_entries[0];
        }
        if (set_entries[1].valid && set_entries[1].v_addr == vpage && set_entries[1].asid == asid &&
            set_entries[1].priv == priv) {
            lru[set] = 0;
            return &set_entries[1];
        }
        return nullptr;
    }

    /**
     * @brief Unified TLB lookup by access kind.
     * @param kind Type of access (instruction, data read, data write).
     * @param vaddr Virtual address.
     * @param asid Address Space Identifier.
     * @param priv Privilege level.
     * @return Pointer to matching TLBEntry if hit, or nullptr on miss.
     */
    [[nodiscard]] inline auto lookup(TlbAccessKind kind, Address vaddr, Asid asid,
                                     PrivilegeLevel priv) -> TLBEntry* {
        switch (kind) {
            case TlbAccessKind::Instruction:
                return lookup<TlbAccessKind::Instruction>(vaddr, asid, priv);
            case TlbAccessKind::DataRead:
                return lookup<TlbAccessKind::DataRead>(vaddr, asid, priv);
            case TlbAccessKind::DataWrite:
                return lookup<TlbAccessKind::DataWrite>(vaddr, asid, priv);
        }
        std::unreachable();
    }

    /**
     * @brief Fast templated TLB insert by access kind using LRU replacement.
     * @tparam Kind Access kind (Instruction, DataRead, DataWrite).
     * @param vaddr Virtual address.
     * @param paddr Physical address.
     * @param asid Address Space Identifier.
     * @param priv Privilege level.
     * @return Pointer to newly inserted TLBEntry.
     */
    template <TlbAccessKind Kind>
    inline auto insert(Address vaddr, Address paddr, Asid asid, PrivilegeLevel priv) -> TLBEntry* {
        auto& table = select_table_static<Kind>();
        auto& lru = select_lru_static<Kind>();
        const auto set = calc_set(vaddr);
        const Address vpage = calc_vpage(vaddr);
        const Address ppage = paddr & ~simrv::memory::kPageMask;
        auto& set_entries = table[set];

        if (set_entries[0].valid && set_entries[0].v_addr == vpage && set_entries[0].asid == asid &&
            set_entries[0].priv == priv) {
            set_entries[0].p_addr = ppage;
            lru[set] = 1;
            return &set_entries[0];
        }
        if (set_entries[1].valid && set_entries[1].v_addr == vpage && set_entries[1].asid == asid &&
            set_entries[1].priv == priv) {
            set_entries[1].p_addr = ppage;
            lru[set] = 0;
            return &set_entries[1];
        }

        const int way = !set_entries[0].valid ? 0 : (!set_entries[1].valid ? 1 : lru[set]);
        auto& entry = set_entries[way];
        entry.v_addr = vpage;
        entry.p_addr = ppage;
        entry.asid = asid;
        entry.priv = priv;
        entry.valid = true;
        lru[set] = 1 - way;
        return &entry;
    }

    /**
     * @brief Unified TLB insert by access kind using LRU replacement.
     * @param kind Type of access (instruction, data read, data write).
     * @param vaddr Virtual address.
     * @param paddr Physical address.
     * @param asid Address Space Identifier.
     * @param priv Privilege level.
     * @return Pointer to newly inserted TLBEntry.
     */
    inline auto insert(TlbAccessKind kind, Address vaddr, Address paddr, Asid asid,
                       PrivilegeLevel priv) -> TLBEntry* {
        switch (kind) {
            case TlbAccessKind::Instruction:
                return insert<TlbAccessKind::Instruction>(vaddr, paddr, asid, priv);
            case TlbAccessKind::DataRead:
                return insert<TlbAccessKind::DataRead>(vaddr, paddr, asid, priv);
            case TlbAccessKind::DataWrite:
                return insert<TlbAccessKind::DataWrite>(vaddr, paddr, asid, priv);
        }
        std::unreachable();
    }

    /// Inspect a translation without updating replacement state or hit counters.
    template <TlbAccessKind Kind>
    [[nodiscard]] constexpr auto peek(Address vaddr, Asid asid, PrivilegeLevel priv) const noexcept
        -> const TLBEntry* {
        const auto& table = select_table_static<Kind>();
        const auto set = calc_set(vaddr);
        const Address vpage = calc_vpage(vaddr);
        for (const auto& entry : table[set]) {
            if (entry.valid && entry.asid == asid && entry.v_addr == vpage && entry.priv == priv) {
                return &entry;
            }
        }
        return nullptr;
    }

    /// Inspect an instruction translation without updating replacement state or hit counters.
    [[nodiscard]] constexpr auto peek_inst_r(Address vaddr, Asid asid,
                                             PrivilegeLevel priv) const noexcept
        -> const TLBEntry* {
        return peek<TlbAccessKind::Instruction>(vaddr, asid, priv);
    }

    /// Inspect a data read translation without updating replacement state or hit counters.
    [[nodiscard]] constexpr auto peek_data_r(Address vaddr, Asid asid,
                                             PrivilegeLevel priv) const noexcept
        -> const TLBEntry* {
        return peek<TlbAccessKind::DataRead>(vaddr, asid, priv);
    }

    /// Inspect a data write translation without updating replacement state or hit counters.
    [[nodiscard]] constexpr auto peek_data_w(Address vaddr, Asid asid,
                                             PrivilegeLevel priv) const noexcept
        -> const TLBEntry* {
        return peek<TlbAccessKind::DataWrite>(vaddr, asid, priv);
    }

    // --- Direct forwarding wrappers (templated, zero-cost) ---

    [[nodiscard]] inline auto lookup_inst_r(Address vaddr, Asid asid, PrivilegeLevel priv)
        -> TLBEntry* {
        return lookup<TlbAccessKind::Instruction>(vaddr, asid, priv);
    }

    inline auto insert_inst_r(Address vaddr, Address paddr, Asid asid, PrivilegeLevel priv)
        -> TLBEntry* {
        return insert<TlbAccessKind::Instruction>(vaddr, paddr, asid, priv);
    }

    [[nodiscard]] inline auto lookup_data_r(Address vaddr, Asid asid, PrivilegeLevel priv)
        -> TLBEntry* {
        return lookup<TlbAccessKind::DataRead>(vaddr, asid, priv);
    }

    inline auto insert_data_r(Address vaddr, Address paddr, Asid asid, PrivilegeLevel priv)
        -> TLBEntry* {
        return insert<TlbAccessKind::DataRead>(vaddr, paddr, asid, priv);
    }

    [[nodiscard]] inline auto lookup_data_w(Address vaddr, Asid asid, PrivilegeLevel priv)
        -> TLBEntry* {
        return lookup<TlbAccessKind::DataWrite>(vaddr, asid, priv);
    }

    inline auto insert_data_w(Address vaddr, Address paddr, Asid asid, PrivilegeLevel priv)
        -> TLBEntry* {
        return insert<TlbAccessKind::DataWrite>(vaddr, paddr, asid, priv);
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

    [[nodiscard]] constexpr auto select_table(TlbAccessKind kind) noexcept
        -> std::array<TLBSet, kNumSets>& {
        switch (kind) {
            case TlbAccessKind::Instruction:
                return inst_r;
            case TlbAccessKind::DataRead:
                return data_r;
            case TlbAccessKind::DataWrite:
                return data_w;
        }
        std::unreachable();
    }

    [[nodiscard]] constexpr auto select_lru(TlbAccessKind kind) noexcept
        -> std::array<uint8_t, kNumSets>& {
        switch (kind) {
            case TlbAccessKind::Instruction:
                return inst_r_lru;
            case TlbAccessKind::DataRead:
                return data_r_lru;
            case TlbAccessKind::DataWrite:
                return data_w_lru;
        }
        std::unreachable();
    }
};

}  // namespace simrv::core
