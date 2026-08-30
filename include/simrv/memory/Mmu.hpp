/**
 * @file Mmu.hpp
 * @brief Memory Management Unit (MMU) for Sv32, Sv39, and Sv48 translation.
 *
 * Provides address translation, page table walking, and virtual memory support
 * for both Linux and RTOS workloads.
 */
#pragma once

#include <expected>

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
    int level = -1;
    bool update_access_bits = true;
    PageWalkStatus status = PageWalkStatus::Fault;
    const core::ArchState* arch_state = nullptr;
};

/**
 * @class Mmu
 * @brief Handles Sv32, Sv39, and Sv48 page translation.
 *
 * Supports supervisor and user privilege levels, implements correct page table
 * access control, and provides identity-mapping support for early Linux boot.
 */
class Mmu {
   public:
    /**
     * @brief Construct an MMU instance.
     * @param mmem Pointer to machine memory
     * @param dram_base Guest physical base of the backing memory
     * @param dram_size Size in bytes of the backing memory
     */
    explicit Mmu(Byte* mmem, Address dram_base, Address dram_size);

    [[nodiscard]] auto mmem() const -> Byte* { return mmem_; }

    /**
     * @brief Perform a page walk using the active supported satp mode.
     *
     * @param v_addr Virtual address to translate
     * @param access Access type (read/write/execute)
     * @param priv Current CPU privilege level
     * @param mstatus Current CPU mstatus register
     * @param satp Current CPU satp register
     * @param xlen Execution width (32 or 64)
     * @param update_access_bits Whether to update page table A/D bits
     * @param arch_state Optional CPU architectural state for PMP enforcement
     * @return Translated physical address or TrapCause on fault
     */
    std::expected<PhysAddr, TrapCause> page_walk(VirtAddr v_addr, PteAccess access,
                                                 PrivilegeLevel priv, CSRValue mstatus, Word satp,
                                                 unsigned xlen, bool update_access_bits = true,
                                                 const core::ArchState* arch_state = nullptr);

    /**
     * @brief Translate address without performing page walk.
     *
     * Returns physical address directly if in machine mode or MMU disabled,
     * otherwise performs full page walk.
     *
     * @param v_addr Virtual address
     * @param access Access type
     * @param priv Current CPU privilege level
     * @param mstatus Current CPU mstatus register
     * @param satp Current CPU satp register
     * @param xlen Execution width (32 or 64)
     * @param update_access_bits Whether to update page table A/D bits
     * @param arch_state Optional CPU architectural state for PMP enforcement
     * @return Translated physical address or TrapCause on fault
     */
    std::expected<PhysAddr, TrapCause> translate(VirtAddr v_addr, PteAccess access,
                                                 PrivilegeLevel priv, CSRValue mstatus, Word satp,
                                                 unsigned xlen, bool update_access_bits = true,
                                                 const core::ArchState* arch_state = nullptr);

    /// Start a walk without performing any implicit physical-memory access.
    [[nodiscard]] PageWalkState begin_page_walk(VirtAddr v_addr, PteAccess access,
                                                PrivilegeLevel priv, CSRValue mstatus, Word satp,
                                                unsigned xlen, bool update_access_bits = true,
                                                const core::ArchState* arch_state = nullptr);

    /// Consume the PTE returned by the current ReadPte request.
    void accept_page_walk_pte(PageWalkState& state, Word pte) const;

    /// Complete the current accessed/dirty-bit WritePte request.
    static void accept_page_walk_write(PageWalkState& state);

    /// Convert a physical PTE transaction failure into the original access-fault class.
    static void fail_page_walk_access(PageWalkState& state);

    /**
     * @brief Verify if a virtual address is canonical according to the active SV mode.
     *
     * @param v_addr Virtual address to verify
     * @param satp Current CPU satp register to determine SV mode
     * @param xlen Current execution XLEN
     * @return true if the address is canonical, false otherwise
     */
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

   private:
    Byte* mmem_;
    Address dram_base_;
    Address dram_size_;

    /**
     * @brief Test whether an implicit page-table access is backed by valid memory and PMA extent.
     *
     * RISC-V page-table walks are physical memory accesses. A PMA/PMP or bus
     * failure during implicit PTE access raises the access-fault exception
     * corresponding to the original instruction, load, or store—not a page fault.
     */
    [[nodiscard]] auto pte_access_valid(PhysAddr address, unsigned size) const -> bool;

    // Per-address-space translation helpers
    /**
     * @brief Validate PTE access permissions for current privilege level.
     *
     * Checks that the PTE has valid permissions bits (XWR), proper privilege
     * level access, and sufficient access rights for the requested operation.
     *
     * @param pte Page table entry value
     * @param permission_bits Extract XWR (execute, write, read) bits from PTE
     * @param access Requested access type (read/write/execute)
     * @param priv Current CPU privilege level
     * @param mstatus Current CPU mstatus register
     * @return true if access is allowed, false if access should fault
     */
    [[nodiscard]] auto validate_pte_permissions(Word pte, Word permission_bits, PteAccess access,
                                                PrivilegeLevel priv, CSRValue mstatus) const
        -> bool;

    [[nodiscard]] static auto page_fault_for(PteAccess access) -> TrapCause;
    [[nodiscard]] static auto access_fault_for(PteAccess access) -> TrapCause;
    void select_next_pte(PageWalkState& state, PhysAddr table_address) const;

    // Page table structure constants
    static constexpr Word kPteShift = 10;  // PPN to PTE conversion shift
};

}  // namespace simrv
