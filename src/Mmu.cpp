/**
 * @file Mmu.cpp
 * @brief Memory Management Unit implementation for SV32 virtual memory.
 */
#include "Mmu.hpp"

#include <cstdint>

#include "Cpu.hpp"
#include "Define.hpp"
#include "MemoryUtil.hpp"
#include "XLen.hpp"

namespace simrv {

// Static member initialization
Word Mmu::s_last_valid_root_ppn = 0;

Mmu::Mmu(CPU& cpu, Byte* mmem) : cpu_(cpu), mmem_(mmem) {}

auto Mmu::translate(Address v_addr, Address* p_addr, PteAccess access) -> bool {
    // Machine mode or MMU disabled: use physical addressing
    if (cpu_.priv == kPrivMachine || (cpu_.satp >> 31) == 0) {
        *p_addr = v_addr;
        return false;
    }

    // Translate through page tables
    return page_walk(v_addr, p_addr, access);
}

auto Mmu::read_level1_pte_with_mirror(Address root_page_table_addr, Word vpn_level1,
                                      Word* out_pte_addr) -> Word {
    constexpr Word kPteSize = 4;
    Word pte_addr = root_page_table_addr + (vpn_level1 * kPteSize);
    Word pte =
        simrv::memory_detail::ram_read_fast(pte_addr, static_cast<Instruction>(Funct3::Lw), mmem_);

    // Check for mirror PTE fallback (Linux kernel high/low mapping convention)
    if (pte == 0) {
        bool should_check_mirror = false;
        Word mirror_vpn_level1 = 0;

        if (vpn_level1 >= kMirrorRangeLowStart && vpn_level1 < kMirrorRangeLowEnd) {
            mirror_vpn_level1 = vpn_level1 + kMirrorOffset;
            should_check_mirror = true;
        } else if (vpn_level1 >= kMirrorRangeHighStart && vpn_level1 < kMirrorRangeHighEnd) {
            mirror_vpn_level1 = vpn_level1 - kMirrorOffset;
            should_check_mirror = true;
        }

        if (should_check_mirror) {
            const Word mirror_pte_addr = root_page_table_addr + (mirror_vpn_level1 * kPteSize);
            const Word mirror_pte = simrv::memory_detail::ram_read_fast(
                mirror_pte_addr, static_cast<Instruction>(Funct3::Lw), mmem_);
            if ((mirror_pte & enum_mask(PteFlag::V)) != 0u) {
                pte = mirror_pte;
                pte_addr = mirror_pte_addr;
            }
        }
    }

    *out_pte_addr = pte_addr;
    return pte;
}

auto Mmu::validate_pte_permissions(Word pte, Word permission_bits, PteAccess access) const -> bool {
    // XWR field must not be reserved values (2, 6)
    if (permission_bits == 2 || permission_bits == 6) {
        return false;
    }

    // Supervisor access to user-only page without SUM
    if (cpu_.priv == kPrivSupervisor && ((pte & enum_mask(PteFlag::U)) != 0u) &&
        ((cpu_.mstatus & enum_mask(MstatusBit::Sum)) == 0u)) {
        return false;
    }

    // User accessing supervisor page
    if (cpu_.priv == kPrivUser && ((pte & enum_mask(PteFlag::U)) == 0u)) {
        return false;
    }

    // Check access permissions
    if (((permission_bits >> static_cast<Word>(access)) & 1) == 0) {
        return false;
    }

    return true;
}

void Mmu::update_pte_access_bits(Address pte_addr, Word& pte_value, PteAccess access) {
    // Update A (accessed) and D (dirty) bits as per RISC-V spec
    Word updated_pte = pte_value | enum_mask(PteFlag::A);
    if (access == PteAccess::Write) {
        updated_pte |= enum_mask(PteFlag::D);
    }

    // Write back only if modified
    if (updated_pte != pte_value) {
        pte_value = updated_pte;
        constexpr Word kPteSize = 4;
        for (std::size_t byte_offset = 0; byte_offset < static_cast<std::size_t>(kPteSize);
             ++byte_offset) {
            mmem_[(pte_addr + byte_offset) & simrv::memory::kDramMask] =
                static_cast<Byte>(static_cast<uint8_t>((updated_pte >> (8 * byte_offset)) & 0xFF));
        }
    }
}

auto Mmu::page_walk(Address v_addr, Address* p_addr, PteAccess access) -> bool {
    const Word vpn_level1 = (v_addr >> 22) & kVpnLevel1Mask;
    const Word vpn_level0 = (v_addr >> 12) & kVpnLevel0Mask;
    const Word root_ppn = (cpu_.satp & kPpnMask);
    auto root_pt_addr = static_cast<Address>(root_ppn << 12);

    // Map root page table address into DRAM if in high kernel virtual space
    if (root_pt_addr >= static_cast<Address>(0xC0000000U) &&
        root_pt_addr < static_cast<Address>(0xC0000000U + simrv::memory::kDramSize)) {
        root_pt_addr -= static_cast<Address>(0x40000000U);
    }

    // Level 1 page table lookup
    Word l1_pte_addr = 0;
    Word l1_pte = read_level1_pte_with_mirror(root_pt_addr, vpn_level1, &l1_pte_addr);

    if ((l1_pte & enum_mask(PteFlag::V)) == 0u) {
        // L1 entry invalid: page fault
        // For Linux early boot: allow identity mapping in kernel physical range.
        if (cpu_.priv == kPrivSupervisor && v_addr >= kKernelPhysBase && v_addr < kDramPhysEnd) {
            *p_addr = v_addr;  // Identity map
            return false;
        }
        *p_addr = 0;
        return true;  // Page fault
    }

    const Word l1_permission_bits =
        (((cpu_.mstatus & enum_mask(MstatusBit::Mxr)) != 0u) ? l1_pte >> 1 | l1_pte >> 3
                                                             : l1_pte >> 1) &
        kPermissionBitsMask;

    // If L1 is leaf: perform megapage translation
    if (l1_permission_bits != 0) {
        if (!validate_pte_permissions(l1_pte, l1_permission_bits, access)) {
            *p_addr = 0;
            return true;  // Access fault
        }

        const Word l1_phys_addr =
            (v_addr & kMegapageOffsetMask) | (((l1_pte >> kPteShift) << 12) & ~kMegapageOffsetMask);
        update_pte_access_bits(l1_pte_addr, l1_pte, access);
        *p_addr = l1_phys_addr;
        return false;  // Success
    }

    // Level 0 page table lookup
    const Word l0_pte_addr = ((l1_pte >> kPteShift) << 12) + (vpn_level0 * 4);
    Word l0_pte = simrv::memory_detail::ram_read_fast(l0_pte_addr,
                                                      static_cast<Instruction>(Funct3::Lw), mmem_);

    if ((l0_pte & enum_mask(PteFlag::V)) == 0u) {
        // L0 entry invalid: page fault
        // For Linux early boot: allow identity mapping in kernel physical range.
        if (cpu_.priv == kPrivSupervisor && v_addr >= kKernelPhysBase && v_addr < kDramPhysEnd) {
            *p_addr = v_addr;  // Identity map
            return false;
        }
        *p_addr = 0;
        return true;  // Page fault
    }

    const Word l0_permission_bits =
        (((cpu_.mstatus & enum_mask(MstatusBit::Mxr)) != 0u) ? l0_pte >> 1 | l0_pte >> 3
                                                             : l0_pte >> 1) &
        kPermissionBitsMask;

    if (!validate_pte_permissions(l0_pte, l0_permission_bits, access)) {
        *p_addr = 0;
        return true;  // Access fault
    }

    const Word l0_phys_addr =
        (v_addr & kPageOffsetMask) | (((l0_pte >> kPteShift) << 12) & ~kPageOffsetMask);
    update_pte_access_bits(l0_pte_addr, l0_pte, access);
    *p_addr = l0_phys_addr;
    return false;  // Success
}

}  // namespace simrv
