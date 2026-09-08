/**
 * @file PageTableWalker.hpp
 * @brief Dedicated page table walk engine for Sv32, Sv39, and Sv48.
 */
#pragma once

#include <expected>

#include "simrv/memory/RamView.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {
struct ArchState;
}  // namespace simrv::core

namespace simrv {

using PteFlags = uint8_t;

enum class PteFlag : PteFlags {
    V = (1 << 0),
    R = (1 << 1),
    W = (1 << 2),
    X = (1 << 3),
    U = (1 << 4),
    A = (1 << 6),
    D = (1 << 7),
};

/**
 * @struct PteView
 * @brief Strongly typed architectural view of a Page Table Entry (PTE) for Sv32/Sv39/Sv48/Sv57.
 */
struct PteView {
    Word raw{0};

    [[nodiscard]] constexpr auto valid() const noexcept -> bool {
        return (raw & enum_mask(PteFlag::V)) != 0;
    }
    [[nodiscard]] constexpr auto readable() const noexcept -> bool {
        return (raw & enum_mask(PteFlag::R)) != 0;
    }
    [[nodiscard]] constexpr auto writable() const noexcept -> bool {
        return (raw & enum_mask(PteFlag::W)) != 0;
    }
    [[nodiscard]] constexpr auto executable() const noexcept -> bool {
        return (raw & enum_mask(PteFlag::X)) != 0;
    }
    [[nodiscard]] constexpr auto user() const noexcept -> bool {
        return (raw & enum_mask(PteFlag::U)) != 0;
    }
    [[nodiscard]] constexpr auto global() const noexcept -> bool { return (raw & (1 << 5)) != 0; }
    [[nodiscard]] constexpr auto accessed() const noexcept -> bool {
        return (raw & enum_mask(PteFlag::A)) != 0;
    }
    [[nodiscard]] constexpr auto dirty() const noexcept -> bool {
        return (raw & enum_mask(PteFlag::D)) != 0;
    }
    [[nodiscard]] constexpr auto rsw() const noexcept -> uint8_t {
        return static_cast<uint8_t>((raw >> 8) & 0x3);
    }
    [[nodiscard]] constexpr auto is_leaf() const noexcept -> bool {
        return readable() || executable();
    }
    [[nodiscard]] constexpr auto is_table() const noexcept -> bool { return valid() && !is_leaf(); }
    [[nodiscard]] constexpr auto ppn() const noexcept -> uint64_t {
        return static_cast<uint64_t>(raw >> 10);
    }
    [[nodiscard]] constexpr auto flags() const noexcept -> PteFlags {
        return static_cast<PteFlags>(raw & 0xFF);
    }
    [[nodiscard]] constexpr auto has_reserved_bits(unsigned pte_size) const noexcept -> bool {
        return pte_size == 8 && (static_cast<uint64_t>(raw) >> 54U) != 0;
    }
};

enum class PteAccess : uint8_t { Read = 0, Write = 1, Code = 2 };

enum class PageWalkStatus : uint8_t { ReadPte, WritePte, Complete, Fault };

/** Architectural state of one resumable hardware page-table walk. */
struct PageWalkState {
    VirtAddr virtual_address{0};
    PhysAddr pte_address{0};
    PhysAddr physical_address{0};
    Word pte{0};
    Word pte_update_mask{0};
    CSRValue mstatus{0};
    PteAccess access = PteAccess::Read;
    PrivilegeLevel privilege{};
    TrapCause fault = 0;
    unsigned xlen = 0;
    unsigned pte_size = 0;
    unsigned vpn_bits_per_level = 0;
    PageTableLevel level = -1;
    bool update_access_bits = true;
    PageWalkStatus status = PageWalkStatus::Fault;
    const core::ArchState* arch_state = nullptr;
};

namespace memory {

/**
 * @class PageTableWalker
 * @brief Handles architectural Sv32, Sv39, and Sv48 hardware page table walks.
 */
class PageTableWalker {
   public:
    constexpr PageTableWalker() noexcept = default;
    explicit constexpr PageTableWalker(RamView ram) noexcept : ram_(ram) {}
    explicit constexpr PageTableWalker(Byte* mmem, Address dram_base, Address dram_size) noexcept
        : ram_(mmem, dram_base, dram_size) {}

    [[nodiscard]] constexpr auto ram_view() const noexcept -> RamView { return ram_; }
    constexpr void set_ram_view(RamView ram) noexcept { ram_ = ram; }

    [[nodiscard]] static constexpr auto is_canonical(VirtAddr v_addr, Word satp, unsigned xlen)
        -> bool {
        if (xlen == 32) {
            return true;
        }
        const Word mode = simrv::xlen::satp_mode(satp, 64);
        if (mode == 8) {  // Sv39
            constexpr Word shift = 64 - 39;
            return (static_cast<SignedWord>(v_addr.raw() << shift) >> shift) ==
                   static_cast<SignedWord>(v_addr.raw());
        }
        if (mode == 9) {  // Sv48
            constexpr Word shift = 64 - 48;
            return (static_cast<SignedWord>(v_addr.raw() << shift) >> shift) ==
                   static_cast<SignedWord>(v_addr.raw());
        }
        return true;
    }

    [[nodiscard]] static auto page_fault_for(PteAccess access) noexcept -> TrapCause;
    [[nodiscard]] static auto access_fault_for(PteAccess access) noexcept -> TrapCause;

    [[nodiscard]] auto begin_walk(VirtAddr v_addr, PteAccess access, PrivilegeLevel priv,
                                  CSRValue mstatus, Word satp, unsigned xlen,
                                  bool update_access_bits = true,
                                  const core::ArchState* arch_state = nullptr) const
        -> PageWalkState;

    void accept_pte(PageWalkState& state, Word pte) const;
    static void accept_write(PageWalkState& state) noexcept;
    static void fail_access(PageWalkState& state) noexcept;

    [[nodiscard]] auto walk(VirtAddr v_addr, PteAccess access, PrivilegeLevel priv,
                            CSRValue mstatus, Word satp, unsigned xlen,
                            bool update_access_bits = true,
                            const core::ArchState* arch_state = nullptr) const
        -> std::expected<PhysAddr, TrapCause>;

   private:
    RamView ram_{};

    [[nodiscard]] auto pte_access_valid(PhysAddr address, unsigned size) const noexcept -> bool;
    [[nodiscard]] auto validate_pte_permissions(Word pte, Word permission_bits, PteAccess access,
                                                PrivilegeLevel priv,
                                                CSRValue mstatus) const noexcept -> bool;
    void select_next_pte(PageWalkState& state, PhysAddr table_address) const;

    static constexpr Word kPteShift = 10;
};

}  // namespace memory

using memory::PageTableWalker;

}  // namespace simrv
