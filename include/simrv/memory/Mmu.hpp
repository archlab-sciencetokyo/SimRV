/**
 * @file Mmu.hpp
 * @brief Memory Management Unit (MMU) for SV32 virtual memory translation.
 *
 * Provides address translation, page table walking, and virtual memory support
 * for both Linux and RTOS workloads.
 */
#pragma once

#include <expected>

#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"

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
     * @brief Construct an MMU instance.
     * @param mmem Pointer to machine memory
     */
    explicit Mmu(Byte* mmem);

    [[nodiscard]] auto mmem() const -> Byte* { return mmem_; }

    /**
     * @brief Perform SV32 page walk and address translation.
     *
     * @param v_addr Virtual address to translate
     * @param p_addr Output: translated physical address
     * @param access Access type (read/write/execute)
     * @param priv Current CPU privilege level
     * @param mstatus Current CPU mstatus register
     * @param satp Current CPU satp register
     * @return Translated physical address or TrapCause on fault
     */
    auto page_walk(Address v_addr, PteAccess access, PrivilegeLevel priv, CSRValue mstatus,
                   Word satp, unsigned xlen = xlen::kXLenBits) -> std::expected<Address, TrapCause>;

    /**
     * @brief Translate address without performing page walk.
     *
     * Returns physical address directly if in machine mode or MMU disabled,
     * otherwise performs full page walk.
     *
     * @param v_addr Virtual address
     * @param p_addr Output: physical address
     * @param access Access type
     * @param priv Current CPU privilege level
     * @param mstatus Current CPU mstatus register
     * @param satp Current CPU satp register
     * @param xlen Current execution XLEN
     * @return Translated physical address or TrapCause on fault
     */
    auto translate(Address v_addr, PteAccess access, PrivilegeLevel priv, CSRValue mstatus,
                   Word satp, unsigned xlen = xlen::kXLenBits) -> std::expected<Address, TrapCause>;

    /**
     * @brief Verify if a virtual address is canonical according to the active SV mode.
     *
     * @param v_addr Virtual address to verify
     * @param satp Current CPU satp register to determine SV mode
     * @param xlen Current execution XLEN
     * @return true if the address is canonical, false otherwise
     */
    [[nodiscard]] static constexpr auto is_canonical(Address v_addr, Word satp, unsigned xlen = xlen::kXLenBits) -> bool {
        if (xlen == 32) {
            return true;
        } else {
            const Word mode = simrv::xlen::satp_mode(satp, 64);
            if (mode == 1) { // SV32 compatibility mode under RV64
                return true;
            } else if (mode == 8) {  // SV39
                constexpr Word shift = 64 - 39;
                return (static_cast<SignedWord>(v_addr << shift) >> shift) ==
                       static_cast<SignedWord>(v_addr);
            } else if (mode == 9) {  // SV48
                constexpr Word shift = 64 - 48;
                return (static_cast<SignedWord>(v_addr << shift) >> shift) ==
                       static_cast<SignedWord>(v_addr);
            }
            return true;
        }
    }

   private:
    Byte* mmem_;

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

    /**
     * @brief Update PTE accessed (A) and dirty (D) flag bits per RISC-V spec.
     *
     * Sets the accessed bit on every access and dirty bit on writes,
     * then writes the updated PTE back to memory.
     *
     * @param pte_addr Physical address of the PTE in memory
     * @param pte_value Page table entry value to update
     * @param access Access type that triggered the update
     * @param pte_size Size of the PTE in bytes (4 or 8)
     */
    void update_pte_access_bits(Address pte_addr, Word& pte_value, PteAccess access, unsigned pte_size);

    // Page table structure constants
    static constexpr Word kPteShift = 10;             // PPN to PTE conversion shift
};

}  // namespace simrv
