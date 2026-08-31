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
     * @brief Compute the TLB set index for a given virtual address.
     * @param vaddr Virtual address.
     * @return Set index in range [0, kNumSets - 1].
     */
    [[nodiscard]] static constexpr inline auto calc_set(Address vaddr) noexcept -> TlbSetIndex {
        return static_cast<TlbSetIndex>((vaddr >> 12) & (kNumSets - 1));
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
     * @brief Unified TLB lookup by access kind.
     * @param kind Type of access (instruction, data read, data write).
     * @param vaddr Virtual address.
     * @param asid Address Space Identifier.
     * @param priv Privilege level.
     * @return Pointer to matching TLBEntry if hit, or nullptr on miss.
     */
    [[nodiscard]] inline auto lookup(TlbAccessKind kind, Address vaddr, Asid asid,
                                     PrivilegeLevel priv) -> TLBEntry* {
        auto& table = select_table(kind);
        auto& lru = select_lru(kind);
        const auto set = calc_set(vaddr);
        const Address vpage = calc_vpage(vaddr);
        for (int i = 0; i < 2; i++) {
            auto& entry = table[set][i];
            if (entry.valid && entry.asid == asid && entry.v_addr == vpage && entry.priv == priv) {
                lru[set] = 1 - i;
                return &entry;
            }
        }
        return nullptr;
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
        auto& table = select_table(kind);
        auto& lru = select_lru(kind);
        const auto set = calc_set(vaddr);
        const int way = lru[set];
        auto& entry = table[set][way];
        entry.v_addr = calc_vpage(vaddr);
        entry.p_addr = paddr & ~simrv::memory::kPageMask;
        entry.asid = asid;
        entry.priv = priv;
        entry.valid = true;
        lru[set] = 1 - way;
        return &entry;
    }

    /// Inspect an instruction translation without updating replacement state or hit counters.
    [[nodiscard]] constexpr auto peek_inst_r(Address vaddr, Asid asid,
                                             PrivilegeLevel priv) const noexcept
        -> const TLBEntry* {
        const auto set = calc_set(vaddr);
        const Address vpage = calc_vpage(vaddr);
        for (const auto& entry : inst_r[set]) {
            if (entry.valid && entry.asid == asid && entry.v_addr == vpage && entry.priv == priv) {
                return &entry;
            }
        }
        return nullptr;
    }

    // --- Legacy forwarding wrappers (inline, zero-cost) ---

    [[nodiscard]] inline auto lookup_inst_r(Address vaddr, Asid asid, PrivilegeLevel priv)
        -> TLBEntry* {
        return lookup(TlbAccessKind::Instruction, vaddr, asid, priv);
    }

    inline auto insert_inst_r(Address vaddr, Address paddr, Asid asid, PrivilegeLevel priv)
        -> TLBEntry* {
        return insert(TlbAccessKind::Instruction, vaddr, paddr, asid, priv);
    }

    [[nodiscard]] inline auto lookup_data_r(Address vaddr, Asid asid, PrivilegeLevel priv)
        -> TLBEntry* {
        return lookup(TlbAccessKind::DataRead, vaddr, asid, priv);
    }

    inline auto insert_data_r(Address vaddr, Address paddr, Asid asid, PrivilegeLevel priv)
        -> TLBEntry* {
        return insert(TlbAccessKind::DataRead, vaddr, paddr, asid, priv);
    }

    [[nodiscard]] inline auto lookup_data_w(Address vaddr, Asid asid, PrivilegeLevel priv)
        -> TLBEntry* {
        return lookup(TlbAccessKind::DataWrite, vaddr, asid, priv);
    }

    inline auto insert_data_w(Address vaddr, Address paddr, Asid asid, PrivilegeLevel priv)
        -> TLBEntry* {
        return insert(TlbAccessKind::DataWrite, vaddr, paddr, asid, priv);
    }

   private:
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
        __builtin_unreachable();
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
        __builtin_unreachable();
    }
};

}  // namespace simrv::core
