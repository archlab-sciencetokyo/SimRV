/**
 * @file PrivilegeMemoryAuditTests.cpp
 * @brief Regression test suite for RISC-V Privileged Architecture and Memory Access audit.
 */
#include <array>
#include <cstdlib>
#include <iostream>
#include <string>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/Pmp.hpp"
#include "simrv/isa/Priv.hpp"
#include "simrv/memory/MemoryAccess.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/Mmu.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// 1. PMP 2GB NAPOT mapping in high memory (0x80000000..0xFFFFFFFF) does not wrap to 0 in RV32
void test_pmp_rv32_2gb_napot_wrap() {
    simrv::core::ArchState state{};
    state.priv = simrv::xlen::PrivilegeLevel::Supervisor;

    // NAPOT 2GB region at 0x80000000.
    // 2GB = 2^31 bytes -> t = 31 - 3 = 28 trailing ones in pmpaddr
    const uint64_t napot_2gb = (0x80000000ULL >> 2) | ((1ULL << 28) - 1);
    state.pmpaddr[0] = static_cast<simrv::xlen::Address>(napot_2gb);
    state.pmpcfg[0] = simrv::core::pmp::kPmpModeNapot | simrv::core::pmp::kPmpR |
                      simrv::core::pmp::kPmpW | simrv::core::pmp::kPmpX;
    state.refresh_pmp_status();

    // In RV32, base (0x80000000) + size (0x80000000) would wrap to 0 if evaluated as uint32_t.
    // With 64-bit bounds, it covers [0x80000000, 0x100000000).
    expect(simrv::core::pmp::check_access(state, 0x80000000, 4, simrv::core::PmpAccessType::Read),
           "PMP NAPOT 2GB: Read at base (0x80000000) matches and succeeds");
    expect(simrv::core::pmp::check_access(state, 0x80001000, 4, simrv::core::PmpAccessType::Write),
           "PMP NAPOT 2GB: Write in middle of 2GB range succeeds");
    expect(
        simrv::core::pmp::check_access(state, 0xFFFFFFFC, 4, simrv::core::PmpAccessType::Execute),
        "PMP NAPOT 2GB: Execute at end of 32-bit address space (0xFFFFFFFC) succeeds without wrap");
    expect(!simrv::core::pmp::check_access(state, 0x7FFFFFFC, 4, simrv::core::PmpAccessType::Read),
           "PMP NAPOT 2GB: Access outside 2GB region (0x7FFFFFFC) is denied in S-mode");
}

// 2. MPRV is gated strictly on M-mode execution
void test_pmp_mprv_only_in_m_mode() {
    simrv::core::ArchState state{};
    state.priv = simrv::xlen::PrivilegeLevel::Supervisor;
    // Set MPRV = 1, but MPP = User (0)
    state.mstatus = std::to_underlying(simrv::core::MstatusBit::Mprv);

    // Entry 0: NA4 at 0x80001000 with Read permission for Supervisor, but NO permissions for User
    state.pmpaddr[0] = static_cast<simrv::xlen::Address>(0x80001000 >> 2);
    state.pmpcfg[0] = simrv::core::pmp::kPmpModeNa4 | simrv::core::pmp::kPmpR;
    state.refresh_pmp_status();

    // Since state.priv is Supervisor (not Machine), MPRV must be ignored.
    // The access should evaluate as Supervisor (permitted) rather than MPP (User, denied).
    expect(simrv::core::pmp::check_access(state, 0x80001000, 4, simrv::core::PmpAccessType::Read),
           "MPRV is ignored outside Machine mode; S-mode access uses S-mode privilege");

    // Switch to Machine mode with MPRV=1, MPP=User:
    state.priv = simrv::xlen::PrivilegeLevel::Machine;
    // With MPRV=1 in Machine mode, data access effective privilege becomes MPP (User).
    // User mode access outside permitted regions (or without user permission) must fail:
    expect(!simrv::core::pmp::check_access(state, 0x80001000, 4, simrv::core::PmpAccessType::Write),
           "MPRV in M-mode enforces MPP (User) and denies ungranted write");
}

// 3. Hardware page-table walk accesses PTEs using Supervisor privilege
void test_mmu_page_walk_uses_supervisor_pmp() {
    std::array<simrv::xlen::Byte, 64 * 1024> memory{};
    const simrv::xlen::Address dram_base = 0x80000000;
    const simrv::xlen::Address dram_size = memory.size();
    simrv::Mmu mmu(memory.data(), dram_base, dram_size);

    simrv::core::ArchState state{};
    state.priv = simrv::xlen::PrivilegeLevel::User;

    const simrv::xlen::Word root_ppn = dram_base >> 12;
    simrv::xlen::Word satp = 0;
    if constexpr (simrv::xlen::kIsXLen64) {
        satp = (8ULL << 60) | root_ppn;  // Sv39
    } else {
        satp = (1ULL << 31) | root_ppn;  // Sv32
    }

    // Set up a valid identity leaf PTE in page table at dram_base
    // In Sv32: PTE = (PPN << 10) | V | R | W | U
    // In Sv64: PTE = (PPN << 10) | V | R | W | U
    const simrv::xlen::Word leaf_pte = (root_ppn << 10) | simrv::PteFlag::V | simrv::PteFlag::R |
                                       simrv::PteFlag::W | simrv::PteFlag::U;
    if constexpr (simrv::xlen::kIsXLen64) {
        // Sv39 root table level 2 -> setup pointing to leaf
        auto* table = reinterpret_cast<uint64_t*>(memory.data());
        table[0] = leaf_pte;
    } else {
        auto* table = reinterpret_cast<uint32_t*>(memory.data());
        table[0] = static_cast<uint32_t>(leaf_pte);
    }

    // Configure PMP: TOR entry allowing S-mode to read page table memory [dram_base, dram_base +
    // 0x1000), but not granting permissions to U-mode outside.
    state.pmpaddr[0] = static_cast<simrv::xlen::Address>(dram_base >> 2);
    state.pmpcfg[0] = simrv::core::pmp::kPmpModeTor;  // [0, dram_base) no perms

    state.pmpaddr[1] = static_cast<simrv::xlen::Address>((dram_base + 0x1000) >> 2);
    state.pmpcfg[1] = simrv::core::pmp::kPmpModeTor | simrv::core::pmp::kPmpR |
                      simrv::core::pmp::kPmpW;  // S-mode R/W
    state.refresh_pmp_status();

    // The user initiates address translation.
    // The page-table walker MUST evaluate the PTE fetch under Supervisor mode.
    auto walk_res = mmu.begin_page_walk(simrv::xlen::VirtAddr{0}, simrv::PteAccess::Read,
                                        simrv::xlen::PrivilegeLevel::User, 0, satp,
                                        simrv::xlen::kXLenBits, false, &state);
    expect(walk_res.status == simrv::PageWalkStatus::ReadPte,
           "Page-table walker successfully reads PTE using implicit Supervisor PMP privilege");
}

// 4. Reserved PTE encodings in translation Step 4
void test_mmu_reserved_pte_rejection() {
    simrv::PteView reserved_leaf{.raw = std::to_underlying(simrv::PteFlag::V | simrv::PteFlag::W)};
    expect(reserved_leaf.is_reserved_encoding(), "PTE with R=0, W=1 is identified as reserved");
    expect(!reserved_leaf.is_leaf(), "PTE with R=0, W=1 is not a valid leaf");
    expect(!reserved_leaf.is_table_pointer(), "PTE with R=0, W=1 is not a valid table pointer");

    // Non-leaf pointer with U bit or A bit set
    simrv::PteView table_ptr_user{.raw = std::to_underlying(simrv::PteFlag::V | simrv::PteFlag::U)};
    expect(table_ptr_user.is_table_pointer(), "Table pointer has R=0, W=0, X=0");
    expect(table_ptr_user.table_pointer_has_reserved_bits(),
           "Table pointer with U=1 is flagged as having reserved bits");

    simrv::PteView table_ptr_clean{.raw = std::to_underlying(simrv::PteFlag::V)};
    expect(table_ptr_clean.is_table_pointer(), "Clean table pointer has V=1, R=W=X=0");
    expect(!table_ptr_clean.table_pointer_has_reserved_bits(),
           "Clean table pointer has no reserved bits");
}

// 5. Bare-metal AMO natural alignment check
void test_baremetal_amo_alignment() {
    using simrv::isa::Funct3;
    expect(!simrv::isa::amo_address_aligned(0x80000001, Funct3::Lw),
           "Word AMO address 0x80000001 is misaligned");
    expect(simrv::isa::amo_address_aligned(0x80000004, Funct3::Lw),
           "Word AMO address 0x80000004 is naturally aligned");
    if constexpr (simrv::xlen::kIsXLen64) {
        expect(!simrv::isa::amo_address_aligned(0x80000004, Funct3::Ld),
               "Doubleword AMO address 0x80000004 is misaligned for 8-byte access");
        expect(simrv::isa::amo_address_aligned(0x80000008, Funct3::Ld),
               "Doubleword AMO address 0x80000008 is naturally aligned");
    }
}

// 6. AMO dual Read + Write PMP validation
void test_amo_pmp_dual_permission() {
    using simrv::core::has_access;
    using simrv::core::PmpAccessType;

    const auto read_only = PmpAccessType::Read;
    const auto read_write = PmpAccessType::Read | PmpAccessType::Write;

    expect(has_access(read_write, PmpAccessType::Read), "ReadWrite grants Read");
    expect(has_access(read_write, PmpAccessType::Write), "ReadWrite grants Write");
    expect(has_access(read_write, read_write), "ReadWrite grants ReadWrite");
    expect(!has_access(read_only, read_write), "ReadOnly fails to grant ReadWrite for AMOs");
}

// 7. PMP Range arithmetic with 64-bit boundaries
void test_pmp_range_arithmetic() {
    simrv::core::pmp::PmpRange r{};
    r.base = 0x80000000ULL;
    r.limit = 0x100000000ULL;  // 2GB span starting at 0x80000000

    expect(r.contains(0x80000000ULL, 4), "PmpRange contains start of 2GB range");
    expect(r.contains(0xFFFFFFFCULL, 4), "PmpRange contains end of 2GB range");
    expect(!r.contains(0x7FFFFFFCULL, 4), "PmpRange rejects address below base");
    expect(!r.overlaps(0x7FFFFFFCULL, 4), "PmpRange does not overlap address below base");
    expect(r.partial_overlap(0x7FFFFFFEULL, 4),
           "PmpRange detects partial overlap straddling base boundary");
}

// 8. Global page (G-bit) semantics in Hardware TLB
void test_tlb_global_mappings() {
    simrv::core::Tlb tlb{};
    constexpr simrv::xlen::Address kVaddr = 0x10000;
    constexpr simrv::xlen::Address kPaddr = 0x80000000;
    constexpr Asid kAsid1 = 1;
    constexpr Asid kAsid2 = 2;

    // Insert a global data translation under ASID 1
    tlb.insert_data_r(kVaddr, kPaddr, kAsid1, simrv::xlen::PrivilegeLevel::Supervisor,
                      /*global=*/true);

    // 1. Hit under different ASID (ASID 2)
    auto* hit = tlb.lookup_data_r(kVaddr, kAsid2, simrv::xlen::PrivilegeLevel::Supervisor);
    expect(hit != nullptr, "TLB global entry hits under different ASID");
    if (hit) {
        expect(hit->global, "TLB entry global flag is set");
        expect(hit->p_addr == kPaddr, "TLB entry physical address matches");
    }

    // 2. SFENCE.VMA with ASID=1 must NOT flush global entries
    tlb.flush_selective(simrv::core::TlbFlushFilter{.vaddr = std::nullopt, .asid = kAsid1});
    expect(tlb.lookup_data_r(kVaddr, kAsid2, simrv::xlen::PrivilegeLevel::Supervisor) != nullptr,
           "ASID-filtered TLB flush preserves global entry");

    // 3. SFENCE.VMA with vaddr and ASID=1 must NOT flush global entries
    tlb.flush_selective(simrv::core::TlbFlushFilter{.vaddr = kVaddr, .asid = kAsid1});
    expect(tlb.lookup_data_r(kVaddr, kAsid2, simrv::xlen::PrivilegeLevel::Supervisor) != nullptr,
           "Page-specific ASID-filtered TLB flush preserves global entry");

    // 4. SFENCE.VMA with vaddr and NO ASID flushes global entry
    tlb.flush_selective(simrv::core::TlbFlushFilter{.vaddr = kVaddr, .asid = std::nullopt});
    expect(tlb.lookup_data_r(kVaddr, kAsid2, simrv::xlen::PrivilegeLevel::Supervisor) == nullptr,
           "Global vaddr flush invalidates global TLB entry");
}

// 9. Global page (G-bit) semantics in Fast-Path Soft TLB
void test_soft_tlb_global_mappings() {
    simrv::core::CPU cpu{};
    constexpr simrv::xlen::Address kVaddr = 0x20000;
    constexpr simrv::xlen::Address kPaddr = 0x80020000;
    constexpr simrv::xlen::Address kVpn = kVaddr >> 12;
    constexpr Asid kAsid1 = 1;
    constexpr Asid kAsid2 = 2;

    const size_t idx = simrv::core::CPU::soft_tlb_index(kVpn);
    cpu.soft_tlb_read[idx].set(kVpn, kAsid1, simrv::xlen::PrivilegeLevel::Supervisor,
                               cpu.soft_tlb_epoch, kPaddr, nullptr, /*is_global=*/true);

    // 1. Matches under different ASID
    expect(cpu.soft_tlb_read[idx].matches(kVpn, kAsid2, simrv::xlen::PrivilegeLevel::Supervisor,
                                          cpu.soft_tlb_epoch),
           "Soft TLB global entry matches under different ASID");

    // 2. ASID flush preserves global entry
    cpu.soft_tlb_flush_selective(
        simrv::core::TlbFlushFilter{.vaddr = std::nullopt, .asid = kAsid1});
    expect(cpu.soft_tlb_read[idx].matches(kVpn, kAsid2, simrv::xlen::PrivilegeLevel::Supervisor,
                                          cpu.soft_tlb_epoch),
           "ASID-filtered soft TLB flush preserves global entry");

    // 3. Page-specific ASID flush preserves global entry
    cpu.soft_tlb_flush_selective(simrv::core::TlbFlushFilter{.vaddr = kVaddr, .asid = kAsid1});
    expect(cpu.soft_tlb_read[idx].matches(kVpn, kAsid2, simrv::xlen::PrivilegeLevel::Supervisor,
                                          cpu.soft_tlb_epoch),
           "Page-specific ASID-filtered soft TLB flush preserves global entry");

    // 4. Page-specific flush without ASID invalidates global entry
    cpu.soft_tlb_flush_selective(
        simrv::core::TlbFlushFilter{.vaddr = kVaddr, .asid = std::nullopt});
    expect(!cpu.soft_tlb_read[idx].matches(kVpn, kAsid2, simrv::xlen::PrivilegeLevel::Supervisor,
                                           cpu.soft_tlb_epoch),
           "Page-specific flush without ASID invalidates soft TLB global entry");
}

// 8. Strong typing & operator overloading enhancements
void test_strong_typing_and_operators() {
    using ::ExceptionCode;
    using ::PmpMode;
    using ::PmpPermission;
    using simrv::core::MipBit;
    using simrv::core::MstatusBit;
    using simrv::core::pmp::PmpConfigView;
    using simrv::isa::FflagsBit;

    // A. MstatusBit and MipBit bitwise operators
    const auto mstatus_bits = MstatusBit::Mie | MstatusBit::Mpie;
    expect((mstatus_bits & MstatusBit::Mie) != 0, "MstatusBit bitwise OR and AND work");
    expect((mstatus_bits & MstatusBit::Mprv) == 0, "MstatusBit bitwise filtering works");

    simrv::xlen::CSRValue mstatus = 0;
    mstatus |= MstatusBit::Mie;
    mstatus |= MstatusBit::Mprv;
    expect((mstatus & MstatusBit::Mie) != 0, "CSRValue |= MstatusBit works");
    expect((mstatus & MstatusBit::Mprv) != 0, "CSRValue & MstatusBit works");
    mstatus &= ~MstatusBit::Mie;
    expect((mstatus & MstatusBit::Mie) == 0, "CSRValue &= ~MstatusBit works");
    expect((mstatus & MstatusBit::Mprv) != 0, "Mprv is preserved after clearing Mie");

    const auto mip_bits = MipBit::Mtip | MipBit::Msip;
    expect((mip_bits & MipBit::Mtip) != 0, "MipBit bitwise operators work");

    // B. ExceptionCode and TrapCause comparisons and conversions
    const simrv::xlen::TrapCause cause_illegal = to_trap_cause(ExceptionCode::IllegalInstruction);
    expect(cause_illegal == ExceptionCode::IllegalInstruction,
           "TrapCause compares equal to matching ExceptionCode");
    expect(cause_illegal != ExceptionCode::Breakpoint,
           "TrapCause compares not equal to different ExceptionCode");
    expect(ExceptionCode::IllegalInstruction == cause_illegal,
           "Symmetric comparison between ExceptionCode and TrapCause works");
    expect(to_exception_code(cause_illegal) == ExceptionCode::IllegalInstruction,
           "to_exception_code converts TrapCause to ExceptionCode");

    // C. FflagsBit bitwise operators
    auto fflags = FflagsBit::Nv | FflagsBit::Dz;
    expect((fflags & FflagsBit::Nv) != 0, "FflagsBit bitwise OR and AND work");
    expect((fflags & FflagsBit::Of) == 0, "FflagsBit masks unasserted flags");
    fflags |= FflagsBit::Uf;
    expect((fflags & FflagsBit::Uf) != 0, "FflagsBit |= works");
    fflags &= ~FflagsBit::Nv;
    expect((fflags & FflagsBit::Nv) == 0, "FflagsBit &= ~works");

    // D. PmpPermission and PmpConfigView
    const auto perm = PmpPermission::Read | PmpPermission::Write;
    expect((perm & PmpPermission::Read) == PmpPermission::Read,
           "PmpPermission bitwise operators work");

    PmpConfigView cfg{.raw = 0x9Fu};  // Lock (0x80) + NAPOT (0x18) + R (1) + W (2) + X (4)
    expect(cfg.is_locked(), "PmpConfigView detects locked entry");
    expect(cfg.mode() == PmpMode::Napot, "PmpConfigView detects NAPOT mode");
    expect(cfg.readable() && cfg.writable() && cfg.executable(),
           "PmpConfigView detects all permissions");

    // E. RegisterFile FP and Vector Proxies
    simrv::core::RegisterFile rf{};
    rf[simrv::xlen::FpRegId::Fa0] = 0x1234567890ABCDEFULL;
    expect(rf[simrv::xlen::FpRegId::Fa0] == 0x1234567890ABCDEFULL,
           "FpRegisterProxy operator[] read/write works");

    simrv::core::VectorRegister vreg{};
    vreg.u8[0] = 0x42;
    rf[simrv::xlen::VecRegId::V0] = vreg;
    expect(rf[simrv::xlen::VecRegId::V0]->u8[0] == 0x42,
           "VecRegisterProxy operator[] read/write works");

    // G. Privileged instruction selectors (Funct12Priv, Funct7Priv) operators
    const uint32_t raw_mret = 0x302;
    expect(raw_mret == simrv::isa::Funct12Priv::Mret, "Funct12Priv operator== works");
    expect(simrv::isa::Funct12Priv::Mret == raw_mret, "Funct12Priv operator== symmetric works");
    expect(raw_mret != simrv::isa::Funct12Priv::Sret, "Funct12Priv operator!= works");

    const uint32_t raw_sfence = 0x09;
    expect(raw_sfence == simrv::isa::Funct7Priv::SfenceVma, "Funct7Priv operator== works");
    expect(raw_sfence != simrv::isa::Funct7Priv{0x00}, "Funct7Priv operator!= works");

    // H. MstatusView and helpers
    simrv::core::MstatusView view{};
    view = view.with_mpp(simrv::xlen::PrivilegeLevel::Supervisor).with_mie(true).with_spie(true);
    expect(view.mpp() == simrv::xlen::PrivilegeLevel::Supervisor, "MstatusView::with_mpp works");
    expect(view.mie(), "MstatusView::with_mie works");
    expect(view.spie(), "MstatusView::with_spie works");
    expect(!view.sie(), "MstatusView::sie remains false");

    simrv::xlen::CSRValue mstatus_raw = view;
    expect(simrv::core::mstatus_mpp(mstatus_raw) == simrv::xlen::PrivilegeLevel::Supervisor,
           "mstatus_mpp extracts Supervisor mode");
    mstatus_raw =
        simrv::core::mstatus_with_spp(mstatus_raw, simrv::xlen::PrivilegeLevel::Supervisor);
    expect(simrv::core::mstatus_spp(mstatus_raw) == simrv::xlen::PrivilegeLevel::Supervisor,
           "mstatus_spp extracts Supervisor mode");
    mstatus_raw = simrv::core::mstatus_with_spp(mstatus_raw, simrv::xlen::PrivilegeLevel::User);
    expect(simrv::core::mstatus_spp(mstatus_raw) == simrv::xlen::PrivilegeLevel::User,
           "mstatus_spp extracts User mode");
}

// 11. SRET SPP reset semantics per Privileged Spec 1.13 Section 3.1.6.1
void test_sret_spp_reset_semantics() {
    using simrv::core::ArchState;
    using simrv::core::MstatusBit;
    using simrv::core::TrapController;

    // Case 1: Hart implements U-mode (MISA.U = 1) -> SRET resets SPP to User mode (0)
    {
        ArchState state{};
        state.misa =
            (simrv::xlen::Word{1} << 20) | (simrv::xlen::Word{1} << 18);  // U and S extensions
        state.mstatus = std::to_underlying(MstatusBit::Spp);              // SPP = 1 (Supervisor)
        state.sepc = 0x80001000;
        TrapController::sret(state);
        expect(state.priv == simrv::xlen::PrivilegeLevel::Supervisor,
               "SRET returns to target privilege indicated by previous SPP");
        expect(
            (state.mstatus & MstatusBit::Spp) == 0,
            "SRET resets SPP to least-privileged supported mode (User) when U-mode is implemented");
    }

    // Case 2: SRET also clears SPP when U-mode is not implemented.
    {
        ArchState state{};
        state.misa = (simrv::xlen::Word{1} << 18);            // S extension only, no U extension
        state.mstatus = std::to_underlying(MstatusBit::Spp);  // SPP = 1
        state.sepc = 0x80001000;
        TrapController::sret(state);
        expect(state.priv == simrv::xlen::PrivilegeLevel::Supervisor,
               "SRET returns to Supervisor mode");
        expect((state.mstatus & MstatusBit::Spp) == 0,
               "SRET clears SPP to zero even when U-mode is absent");
    }
}

}  // namespace

auto main() -> int {
    std::cout << "Running PrivilegeMemoryAuditTests...\n";
    test_pmp_rv32_2gb_napot_wrap();
    test_pmp_mprv_only_in_m_mode();
    test_mmu_page_walk_uses_supervisor_pmp();
    test_mmu_reserved_pte_rejection();
    test_baremetal_amo_alignment();
    test_amo_pmp_dual_permission();
    test_pmp_range_arithmetic();
    test_tlb_global_mappings();
    test_soft_tlb_global_mappings();
    test_strong_typing_and_operators();
    test_sret_spp_reset_semantics();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "All PrivilegeMemoryAuditTests passed successfully!\n";
    return EXIT_SUCCESS;
}
