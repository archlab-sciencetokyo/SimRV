/**
 * @file Mmu.hpp
 * @brief Memory Management Unit (MMU) for SV32 virtual memory translation.
 *
 * Provides address translation, page table walking, and virtual memory support
 * for both Linux and RTOS workloads.
 */
#pragma once

#include "Cpu.hpp"
#include "MemoryUtil.hpp"

namespace simrv {

/**
 * @class Mmu
 * @brief Handles SV32 page translation and virtual memory management.
 *
 * Supports supervisor and user privilege levels, implements correct page table
 * access control, and provides identity-mapping support for early Linux boot.
 */
class Mmu {
   public:
    /**
     * @brief Construct an MMU instance bound to a CPU.
     * @param cpu Reference to the CPU state
     * @param mmem Pointer to machine memory
     */
    explicit Mmu(CPU& cpu, Byte* mmem);

    /**
     * @brief Perform SV32 page walk and address translation.
     *
     * @param v_addr Virtual address to translate
     * @param p_addr Output: translated physical address
     * @param access Access type (read/write/execute)
     * @return true if page fault occurred, false on successful translation
     */
    bool page_walk(Address v_addr, Address* p_addr, PteAccess access);

    /**
     * @brief Translate address without performing page walk.
     *
     * Returns physical address directly if in machine mode or MMU disabled,
     * otherwise performs full page walk.
     *
     * @param v_addr Virtual address
     * @param p_addr Output: physical address
     * @param access Access type
     * @return true if translation resulted in page fault
     */
    bool translate(Address v_addr, Address* p_addr, PteAccess access);

   private:
    CPU& cpu_;
    Byte* mmem_;

    // Early boot support: cache last valid root page table for fallback
    static Word s_last_valid_root_ppn;

    // Per-address-space translation helpers
    /**
     * @brief Read Level-1 PTE with mirror mapping fallback for Linux.
     *
     * Attempts to read L1 PTE at the given VPN. If invalid, checks for a
     * mirror PTE in the 0x100-VPN range (used for Linux kernel high/low mapping).
     *
     * @param root_page_table_addr Physical address of root page table
     * @param vpn_level1 Virtual page number at level 1 (bits [31:22])
     * @param out_pte_addr Output: physical address where PTE was found
     * @return PTE value (or mirrored PTE if fallback was used)
     */
    Word read_level1_pte_with_mirror(Address root_page_table_addr, Word vpn_level1,
                                     Word* out_pte_addr);

    /**
     * @brief Validate PTE access permissions for current privilege level.
     *
     * Checks that the PTE has valid permissions bits (XWR), proper privilege
     * level access, and sufficient access rights for the requested operation.
     *
     * @param pte Page table entry value
     * @param permission_bits Extract XWR (execute, write, read) bits from PTE
     * @param access Requested access type (read/write/execute)
     * @return true if access is allowed, false if access should fault
     */
    bool validate_pte_permissions(Word pte, Word permission_bits, PteAccess access) const;

    /**
     * @brief Update PTE accessed (A) and dirty (D) flag bits per RISC-V spec.
     *
     * Sets the accessed bit on every access and dirty bit on writes,
     * then writes the updated PTE back to memory.
     *
     * @param pte_addr Physical address of the PTE in memory
     * @param pte_value Page table entry value to update
     * @param access Access type that triggered the update
     */
    void update_pte_access_bits(Address pte_addr, Word& pte_value, PteAccess access);

    // Page table structure constants
    static constexpr Word kVpnLevel1Mask = 0x3FF;          // 10 bits for L1 VPN
    static constexpr Word kVpnLevel0Mask = 0x3FF;          // 10 bits for L0 VPN
    static constexpr Word kPpnMask = 0x3FFFFF;             // 22 bits for PPN
    static constexpr Word kPermissionBitsMask = 0x7;       // 3 bits for XWR
    static constexpr Word kMegapageOffsetMask = 0x3FFFFF;  // 22-bit offset in megapage
    static constexpr Word kPageOffsetMask = 0xFFF;         // 12-bit offset in page
    static constexpr Word kPteShift = 10;                  // PPN to PTE conversion shift

    // Mirror mapping ranges (Linux kernel high/low convention)
    static constexpr Word kMirrorRangeLowStart = 0x200;   // Mirror low range start
    static constexpr Word kMirrorRangeLowEnd = 0x300;     // Mirror low range end
    static constexpr Word kMirrorRangeHighStart = 0x300;  // Mirror high range start
    static constexpr Word kMirrorRangeHighEnd = 0x400;    // Mirror high range end
    static constexpr Word kMirrorOffset = 0x100;          // VPN offset for mirror

    // Early boot support
    static constexpr Address kKernelPhysBase = static_cast<Address>(0x80400000u);
    static constexpr Address kDramPhysEnd =
        simrv::memory::kDramBaseAddress + simrv::memory::kDramSize;
};

}  // namespace simrv
