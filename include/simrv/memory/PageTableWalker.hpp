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

namespace simrv::memory {

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

/**
 * @enum PtePbmt
 * @brief RISC-V Svpbmt Page-Based Memory Types (PTE bits 62:61) per Privileged ISA 1.13.
 */
enum class PtePbmt : uint8_t {
    Pma = 0,           ///< Standard physical memory attribute
    NonCacheable = 1,  ///< Non-cacheable, idempotent memory (NC)
    Io = 2,            ///< I/O memory, non-cacheable non-idempotent (IO)
    Reserved = 3,      ///< Reserved encoding
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
        constexpr uint64_t kPpnMask = (1ULL << 44) - 1ULL;
        return static_cast<simrv::xlen::PpnIndex>((raw >> 10) & kPpnMask);
    }
    [[nodiscard]] constexpr auto flags() const noexcept -> PteFlags {
        return static_cast<PteFlags>(raw & 0xFF);
    }
    [[nodiscard]] constexpr auto pbmt() const noexcept -> PtePbmt {
        return static_cast<PtePbmt>((static_cast<uint64_t>(raw) >> 61U) & 0x3U);
    }
    [[nodiscard]] constexpr auto is_napot() const noexcept -> bool {
        return ((static_cast<uint64_t>(raw) >> 63U) & 0x1U) != 0;
    }
    [[nodiscard]] constexpr auto has_valid_napot_ppn_encoding() const noexcept -> bool {
        return (ppn() & 0xFU) == 0x8U;
    }
    [[nodiscard]] constexpr auto has_reserved_bits(unsigned pte_size) const noexcept -> bool {
        if (pte_size == 8) {
            constexpr uint64_t kReservedMask = (0x7FULL << 54U);
            const bool reserved_fields = (static_cast<uint64_t>(raw) & kReservedMask) != 0;
            const bool invalid_pbmt = (pbmt() == PtePbmt::Reserved);
            return reserved_fields || invalid_pbmt;
        }
        return false;
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
    simrv::xlen::CsrValue mstatus{0};
    PteAccess access = PteAccess::Read;
    simrv::xlen::PrivilegeLevel privilege{};
    simrv::xlen::TrapCause fault = 0;
    unsigned xlen = 0;
    unsigned pte_size = 0;
    unsigned vpn_bits_per_level = 0;
    simrv::xlen::PageTableLevel level = -1;
    simrv::xlen::Word satp_root{0};
    simrv::xlen::Word hgatp{0};
    simrv::xlen::Address pte_guest_address{0};
    simrv::xlen::Address guest_fault_address{0};
    bool guest_stage_enabled{false};
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
                                  simrv::xlen::PrivilegeLevel priv, simrv::xlen::CsrValue mstatus,
                                  simrv::xlen::Word satp, unsigned xlen,
                                  bool update_access_bits = true,
                                  const core::ArchState* arch_state = nullptr) const
        -> PageWalkState;

    void accept_pte(PageWalkState& state, simrv::xlen::Word pte) const;
    static void accept_write(PageWalkState& state) noexcept;
    static void fail_access(PageWalkState& state) noexcept;

    [[nodiscard]] auto walk(simrv::xlen::VirtAddr v_addr, PteAccess access,
                            simrv::xlen::PrivilegeLevel priv, simrv::xlen::CsrValue mstatus,
                            simrv::xlen::Word satp, unsigned xlen, bool update_access_bits = true,
                            const core::ArchState* arch_state = nullptr,
                            simrv::xlen::Address* guest_fault_address = nullptr) const
        -> std::expected<PageWalkResult, simrv::xlen::TrapCause>;

    /** Translate a guest-physical address through an SvNNx4 hgatp G-stage page table. */
    [[nodiscard]] auto walk_guest_physical(simrv::xlen::Address guest_address, PteAccess access,
                                           simrv::xlen::Word hgatp, unsigned xlen,
                                           simrv::xlen::Address* fault_address = nullptr) const
        -> std::expected<PageWalkResult, simrv::xlen::TrapCause>;

    void flush_pdc() noexcept {
        if (++pdc_epoch_ == 0) {
            pdc_epoch_ = 1;
            pdc_.fill({});
        }
    }

   private:
    struct PdcEntry {
        simrv::xlen::Word satp_root{0};
        simrv::xlen::Word vpn_prefix{0};
        simrv::xlen::PhysAddr table_address{0};
        uint32_t epoch{0};
    };

    static constexpr size_t kPdcSize = 256;
    mutable std::array<PdcEntry, kPdcSize> pdc_{};
    mutable uint32_t pdc_epoch_{1};
    RamView ram_{};

    [[nodiscard]] auto pte_access_valid(simrv::xlen::PhysAddr address, unsigned size) const noexcept
        -> bool;
    [[nodiscard]] auto validate_pte_permissions(simrv::xlen::Word pte,
                                                simrv::xlen::Word permission_bits, PteAccess access,
                                                simrv::xlen::PrivilegeLevel priv,
                                                simrv::xlen::CsrValue mstatus) const noexcept
        -> bool;
    void select_next_pte(PageWalkState& state, simrv::xlen::PhysAddr table_address) const;

    static constexpr simrv::xlen::Word kPteShift = 10;
};

}  // namespace simrv::memory

namespace simrv {
using memory::PageTableWalker;
using memory::PageWalkResult;
using memory::PageWalkState;
using memory::PageWalkStatus;
using memory::PteAccess;
using memory::PteFlag;
using memory::PteFlags;
using memory::PtePbmt;
using memory::PteView;
}  // namespace simrv
