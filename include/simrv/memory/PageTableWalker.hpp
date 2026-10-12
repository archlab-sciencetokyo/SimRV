/**
 * @file PageTableWalker.hpp
 * @brief Dedicated page table walk engine for Sv32, Sv39, and Sv48.
 */
#pragma once

#include <expected>

#include "simrv/memory/RamView.hpp"
#include "simrv/xlen/AddressTypes.hpp"
#include "simrv/xlen/Constants.hpp"

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
    G = (1 << 5),
    A = (1 << 6),
    D = (1 << 7),
};

constexpr auto operator|(PteFlag lhs, PteFlag rhs) noexcept -> PteFlag {
    return static_cast<PteFlag>(std::to_underlying(lhs) | std::to_underlying(rhs));
}

constexpr auto operator&(PteFlag lhs, PteFlag rhs) noexcept -> PteFlag {
    return static_cast<PteFlag>(std::to_underlying(lhs) & std::to_underlying(rhs));
}

constexpr auto operator~(PteFlag flag) noexcept -> PteFlag {
    return static_cast<PteFlag>(~std::to_underlying(flag));
}

constexpr auto operator|(simrv::xlen::Word lhs, PteFlag rhs) noexcept -> simrv::xlen::Word {
    return lhs | std::to_underlying(rhs);
}
constexpr auto operator|(PteFlag lhs, simrv::xlen::Word rhs) noexcept -> simrv::xlen::Word {
    return std::to_underlying(lhs) | rhs;
}
constexpr auto operator|=(simrv::xlen::Word& lhs, PteFlag rhs) noexcept -> simrv::xlen::Word& {
    lhs |= std::to_underlying(rhs);
    return lhs;
}
constexpr auto operator&(simrv::xlen::Word lhs, PteFlag rhs) noexcept -> simrv::xlen::Word {
    return lhs & std::to_underlying(rhs);
}
constexpr auto operator&(PteFlag lhs, simrv::xlen::Word rhs) noexcept -> simrv::xlen::Word {
    return std::to_underlying(lhs) & rhs;
}
constexpr auto operator&=(simrv::xlen::Word& lhs, PteFlag rhs) noexcept -> simrv::xlen::Word& {
    lhs &= std::to_underlying(rhs);
    return lhs;
}

/**
 * @struct PteView
 * @brief Strongly typed architectural view of a Page Table Entry (PTE) for Sv32/Sv39/Sv48/Sv57.
 */
struct PteView {
    simrv::xlen::Word raw{0};

    [[nodiscard]] constexpr auto valid() const noexcept -> bool { return (raw & PteFlag::V) != 0; }
    [[nodiscard]] constexpr auto readable() const noexcept -> bool {
        return (raw & PteFlag::R) != 0;
    }
    [[nodiscard]] constexpr auto writable() const noexcept -> bool {
        return (raw & PteFlag::W) != 0;
    }
    [[nodiscard]] constexpr auto executable() const noexcept -> bool {
        return (raw & PteFlag::X) != 0;
    }
    [[nodiscard]] constexpr auto user() const noexcept -> bool { return (raw & PteFlag::U) != 0; }
    [[nodiscard]] constexpr auto global() const noexcept -> bool { return (raw & PteFlag::G) != 0; }
    [[nodiscard]] constexpr auto accessed() const noexcept -> bool {
        return (raw & PteFlag::A) != 0;
    }
    [[nodiscard]] constexpr auto dirty() const noexcept -> bool { return (raw & PteFlag::D) != 0; }
    [[nodiscard]] constexpr auto rsw() const noexcept -> uint8_t {
        return static_cast<uint8_t>((raw >> 8) & 0x3);
    }
    [[nodiscard]] constexpr auto is_leaf() const noexcept -> bool {
        return valid() && (readable() || executable());
    }
    [[nodiscard]] constexpr auto is_table_pointer() const noexcept -> bool {
        return valid() && !readable() && !writable() && !executable();
    }
    [[nodiscard]] constexpr auto is_table() const noexcept -> bool { return is_table_pointer(); }
    [[nodiscard]] constexpr auto is_reserved_encoding() const noexcept -> bool {
        return valid() && !readable() && writable();
    }
    [[nodiscard]] constexpr auto table_pointer_has_reserved_bits() const noexcept -> bool {
        return (raw & (PteFlag::U | PteFlag::A | PteFlag::D)) != 0;
    }
    [[nodiscard]] constexpr auto ppn() const noexcept -> simrv::xlen::PpnIndex {
        return static_cast<simrv::xlen::PpnIndex>(raw >> 10);
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
    simrv::xlen::VirtAddr virtual_address{0};
    simrv::xlen::PhysAddr pte_address{0};
    simrv::xlen::PhysAddr physical_address{0};
    simrv::xlen::Word pte{0};
    simrv::xlen::Word pte_update_mask{0};
    simrv::xlen::CSRValue mstatus{0};
    PteAccess access = PteAccess::Read;
    simrv::xlen::PrivilegeLevel privilege{};
    simrv::xlen::TrapCause fault = 0;
    unsigned xlen = 0;
    unsigned pte_size = 0;
    unsigned vpn_bits_per_level = 0;
    simrv::xlen::PageTableLevel level = -1;
    bool update_access_bits = true;
    PageWalkStatus status = PageWalkStatus::Fault;
    const core::ArchState* arch_state = nullptr;
};

struct PageWalkResult {
    simrv::xlen::PhysAddr physical_address{0};
    simrv::xlen::Word pte{0};

    [[nodiscard]] constexpr auto raw() const noexcept -> simrv::xlen::Address {
        return physical_address.raw();
    }
    [[nodiscard]] constexpr auto global() const noexcept -> bool {
        return PteView{.raw = pte}.global();
    }
    constexpr operator simrv::xlen::PhysAddr() const noexcept { return physical_address; }
    constexpr auto operator<=>(const PageWalkResult&) const noexcept = default;
    constexpr bool operator==(const simrv::xlen::PhysAddr& other) const noexcept {
        return physical_address == other;
    }
    constexpr bool operator==(simrv::xlen::Address other) const noexcept {
        return physical_address.raw() == other;
    }
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
    explicit constexpr PageTableWalker(simrv::xlen::Byte* mmem, simrv::xlen::Address dram_base,
                                       simrv::xlen::Address dram_size) noexcept
        : ram_(mmem, dram_base, dram_size) {}

    [[nodiscard]] constexpr auto ram_view() const noexcept -> RamView { return ram_; }
    constexpr void set_ram_view(RamView ram) noexcept { ram_ = ram; }

    [[nodiscard]] static constexpr auto is_canonical(simrv::xlen::VirtAddr v_addr,
                                                     simrv::xlen::Word satp, unsigned xlen)
        -> bool {
        if (xlen == 32) {
            return true;
        }
        const simrv::xlen::Word mode = simrv::xlen::satp_mode(satp, 64);
        if (mode == 8) {  // Sv39
            constexpr simrv::xlen::Word shift = 64 - 39;
            return (static_cast<simrv::xlen::SignedWord>(v_addr.raw() << shift) >> shift) ==
                   static_cast<simrv::xlen::SignedWord>(v_addr.raw());
        }
        if (mode == 9) {  // Sv48
            constexpr simrv::xlen::Word shift = 64 - 48;
            return (static_cast<simrv::xlen::SignedWord>(v_addr.raw() << shift) >> shift) ==
                   static_cast<simrv::xlen::SignedWord>(v_addr.raw());
        }
        if (mode == 10) {  // Sv57
            constexpr simrv::xlen::Word shift = 64 - 57;
            return (static_cast<simrv::xlen::SignedWord>(v_addr.raw() << shift) >> shift) ==
                   static_cast<simrv::xlen::SignedWord>(v_addr.raw());
        }
        return true;
    }

    [[nodiscard]] static auto page_fault_for(PteAccess access) noexcept -> simrv::xlen::TrapCause;
    [[nodiscard]] static auto access_fault_for(PteAccess access) noexcept -> simrv::xlen::TrapCause;

    [[nodiscard]] auto begin_walk(simrv::xlen::VirtAddr v_addr, PteAccess access,
                                  simrv::xlen::PrivilegeLevel priv, simrv::xlen::CSRValue mstatus,
                                  simrv::xlen::Word satp, unsigned xlen,
                                  bool update_access_bits = true,
                                  const core::ArchState* arch_state = nullptr) const
        -> PageWalkState;

    void accept_pte(PageWalkState& state, simrv::xlen::Word pte) const;
    static void accept_write(PageWalkState& state) noexcept;
    static void fail_access(PageWalkState& state) noexcept;

    [[nodiscard]] auto walk(simrv::xlen::VirtAddr v_addr, PteAccess access,
                            simrv::xlen::PrivilegeLevel priv, simrv::xlen::CSRValue mstatus,
                            simrv::xlen::Word satp, unsigned xlen, bool update_access_bits = true,
                            const core::ArchState* arch_state = nullptr) const
        -> std::expected<PageWalkResult, simrv::xlen::TrapCause>;

   private:
    RamView ram_{};

    [[nodiscard]] auto pte_access_valid(simrv::xlen::PhysAddr address, unsigned size) const noexcept
        -> bool;
    [[nodiscard]] auto validate_pte_permissions(simrv::xlen::Word pte,
                                                simrv::xlen::Word permission_bits, PteAccess access,
                                                simrv::xlen::PrivilegeLevel priv,
                                                simrv::xlen::CSRValue mstatus) const noexcept
        -> bool;
    void select_next_pte(PageWalkState& state, simrv::xlen::PhysAddr table_address) const;

    static constexpr simrv::xlen::Word kPteShift = 10;
};

}  // namespace memory

}  // namespace simrv
