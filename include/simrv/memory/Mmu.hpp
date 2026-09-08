/**
 * @file Mmu.hpp
 * @brief Memory Management Unit (MMU) for Sv32, Sv39, and Sv48 translation.
 *
 * Provides address translation, page table walking, and virtual memory support
 * for both Linux and RTOS workloads.
 */
#pragma once

#include <expected>

#include "simrv/memory/PageTableWalker.hpp"
#include "simrv/memory/RamView.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {
struct ArchState;
}  // namespace simrv::core

namespace simrv {

/**
 * @class Mmu
 * @brief Handles Sv32, Sv39, and Sv48 page translation.
 *
 * Supports supervisor and user privilege levels, implements correct page table
 * access control, and provides identity-mapping support for early Linux boot.
 */
class Mmu {
   public:
    explicit Mmu(memory::RamView ram = {}) noexcept : walker_(ram) {}
    explicit Mmu(Byte* mmem, Address dram_base, Address dram_size) noexcept
        : walker_(mmem, dram_base, dram_size) {}

    [[nodiscard]] auto walker() const noexcept -> const memory::PageTableWalker& { return walker_; }
    [[nodiscard]] auto walker() noexcept -> memory::PageTableWalker& { return walker_; }

    [[nodiscard]] auto mmem() const noexcept -> Byte* { return walker_.ram_view().data(); }
    [[nodiscard]] auto ram_view() const noexcept -> memory::RamView { return walker_.ram_view(); }
    void set_ram_view(memory::RamView ram) noexcept { walker_.set_ram_view(ram); }

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
                                                const core::ArchState* arch_state = nullptr) const;

    /// Consume the PTE returned by the current ReadPte request.
    void accept_page_walk_pte(PageWalkState& state, Word pte) const;

    /// Complete the current accessed/dirty-bit WritePte request.
    static void accept_page_walk_write(PageWalkState& state) noexcept;

    /// Convert a physical PTE transaction failure into the original access-fault class.
    static void fail_page_walk_access(PageWalkState& state) noexcept;

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
        return memory::PageTableWalker::is_canonical(v_addr, satp, xlen);
    }

   private:
    memory::PageTableWalker walker_;
};

}  // namespace simrv
