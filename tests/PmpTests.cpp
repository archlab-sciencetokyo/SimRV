#include <array>
#include <cstdlib>
#include <iostream>
#include <string>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Pmp.hpp"
#include "simrv/memory/MemoryAccess.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/memory/Mmu.hpp"

namespace {

auto failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_pmp_matching_modes() {
    simrv::core::ArchState state{};
    state.priv = PrivilegeLevel::Supervisor;

    // Default: No PMP entries active -> All S-mode accesses allowed
    expect(simrv::core::pmp::check_access(state, 0x80000000, 4, simrv::core::PmpAccessType::Read),
           "Default: Empty PMP allows S-mode read");
    expect(
        simrv::core::pmp::check_access(state, 0x80000000, 4, simrv::core::PmpAccessType::Execute),
        "Default: Empty PMP allows S-mode execute");

    // 1. NA4 mode at 0x80001000 (4-byte range [0x80001000, 0x80001004))
    state.pmpaddr[0] = static_cast<Address>(0x80001000 >> 2);
    state.pmpcfg[0] =
        simrv::core::pmp::kPmpModeNa4 | simrv::core::pmp::kPmpR | simrv::core::pmp::kPmpX;
    state.refresh_pmp_status();

    expect(simrv::core::pmp::check_access(state, 0x80001000, 4, simrv::core::PmpAccessType::Read),
           "NA4: Read inside NA4 region allowed");
    expect(
        simrv::core::pmp::check_access(state, 0x80001000, 4, simrv::core::PmpAccessType::Execute),
        "NA4: Execute inside NA4 region allowed");
    expect(!simrv::core::pmp::check_access(state, 0x80001000, 4, simrv::core::PmpAccessType::Write),
           "NA4: Write without W bit denied");
    expect(!simrv::core::pmp::check_access(state, 0x80001004, 4, simrv::core::PmpAccessType::Read),
           "NA4: Access outside configured regions denied in S-mode");

    // 2. TOR mode: Entry 1 with TOR mode spanning [0x80001000, 0x80002000)
    // Entry 0 pmpaddr was 0x80001000 >> 2. Entry 1 pmpaddr is 0x80002000 >> 2.
    state.pmpaddr[1] = static_cast<Address>(0x80002000 >> 2);
    state.pmpcfg[1] =
        simrv::core::pmp::kPmpModeTor | simrv::core::pmp::kPmpR | simrv::core::pmp::kPmpW;
    state.refresh_pmp_status();

    // Lower-numbered entry 0 takes priority for [0x80001000, 0x80001004)
    expect(!simrv::core::pmp::check_access(state, 0x80001000, 4, simrv::core::PmpAccessType::Write),
           "Priority: Entry 0 has priority over Entry 1 on overlapping base");
    // Entry 1 covers [0x80001004, 0x80002000)
    expect(simrv::core::pmp::check_access(state, 0x80001004, 4, simrv::core::PmpAccessType::Write),
           "TOR: Write within entry 1 range allowed");
    expect(
        !simrv::core::pmp::check_access(state, 0x80001004, 4, simrv::core::PmpAccessType::Execute),
        "TOR: Execute without X bit denied in entry 1 range");

    // 3. NAPOT mode: Entry 2 64KB region at 0x80010000
    // 64KB = 2^16 bytes. NAPOT: t = 13 trailing ones in pmpaddr
    const uint64_t napot_64k = (0x80010000ULL >> 2) | ((1ULL << 13) - 1);
    state.pmpaddr[2] = static_cast<Address>(napot_64k);
    state.pmpcfg[2] = simrv::core::pmp::kPmpModeNapot | simrv::core::pmp::kPmpR |
                      simrv::core::pmp::kPmpW | simrv::core::pmp::kPmpX;
    state.refresh_pmp_status();

    expect(simrv::core::pmp::check_access(state, 0x80010000, 4, simrv::core::PmpAccessType::Read),
           "NAPOT: Read at start of 64KB range allowed");
    expect(simrv::core::pmp::check_access(state, 0x8001FFFC, 4, simrv::core::PmpAccessType::Write),
           "NAPOT: Write at end of 64KB range allowed");
    expect(
        simrv::core::pmp::check_access(state, 0x80018000, 4, simrv::core::PmpAccessType::Execute),
        "NAPOT: Execute in middle of 64KB range allowed");
    expect(!simrv::core::pmp::check_access(state, 0x80020000, 4, simrv::core::PmpAccessType::Read),
           "NAPOT: Access past 64KB boundary denied");
}

void test_pmp_partial_overlap() {
    simrv::core::ArchState state{};
    state.priv = PrivilegeLevel::Supervisor;

    // PMP entry 0: NA4 at 0x80001000 ([0x80001000, 0x80001004)) with RWX
    state.pmpaddr[0] = static_cast<Address>(0x80001000 >> 2);
    state.pmpcfg[0] = simrv::core::pmp::kPmpModeNa4 | simrv::core::pmp::kPmpR |
                      simrv::core::pmp::kPmpW | simrv::core::pmp::kPmpX;
    state.refresh_pmp_status();

    // 8-byte access at 0x80001000 spans [0x80001000, 0x80001008), partially outside [0x80001000,
    // 0x80001004) Per RISC-V Spec: partial overlap must fail immediately!
    expect(!simrv::core::pmp::check_access(state, 0x80001000, 8, simrv::core::PmpAccessType::Read),
           "Partial overlap: 8-byte read across 4-byte PMP boundary fails");
    expect(!simrv::core::pmp::check_access(state, 0x80000FFE, 4, simrv::core::PmpAccessType::Read),
           "Partial overlap: Read crossing into start of PMP region fails");
}

void test_pmp_machine_mode_and_locks() {
    simrv::core::ArchState state{};
    state.priv = PrivilegeLevel::Machine;

    // Entry 0: NA4 at 0x80001000 with NO permissions, unlocked (L = 0)
    state.pmpaddr[0] = static_cast<Address>(0x80001000 >> 2);
    state.pmpcfg[0] = simrv::core::pmp::kPmpModeNa4;  // No R, W, X
    state.refresh_pmp_status();

    // In M-mode with L = 0, access bypasses PMP
    expect(simrv::core::pmp::check_access(state, 0x80001000, 4, simrv::core::PmpAccessType::Read),
           "M-mode bypasses unlocked PMP entry");
    expect(simrv::core::pmp::check_access(state, 0x80001000, 4, simrv::core::PmpAccessType::Write),
           "M-mode bypasses unlocked PMP entry for write");

    // Lock entry 0 (L = 1)
    state.pmpcfg[0] |= simrv::core::pmp::kPmpL;
    state.refresh_pmp_status();

    // Now in M-mode, locked entry 0 enforces permissions (denies read/write since R=0, W=0)
    expect(!simrv::core::pmp::check_access(state, 0x80001000, 4, simrv::core::PmpAccessType::Read),
           "Locked PMP entry enforces permission in M-mode");
    expect(!simrv::core::pmp::check_access(state, 0x80001000, 4, simrv::core::PmpAccessType::Write),
           "Locked PMP entry denies write in M-mode without W flag");

    // Add Read permission to locked entry
    state.pmpcfg[0] |= simrv::core::pmp::kPmpR;
    state.refresh_pmp_status();
    expect(simrv::core::pmp::check_access(state, 0x80001000, 4, simrv::core::PmpAccessType::Read),
           "Locked PMP entry permits read in M-mode when R flag is set");
}

void test_pmp_csr_locking_and_updates() {
    simrv::core::CPU cpu{};
    cpu.reset();

    // Write pmpcfg0 and pmpaddr0
    const Address test_addr = 0x80001000 >> 2;
    auto res_addr = cpu.write_csr(0x3B0, static_cast<CSRValue>(test_addr));
    expect(res_addr.has_value(), "Write to pmpaddr0 succeeded");

    // Write pmpcfg0 with L = 1, NA4, R
    const CSRValue cfg_val =
        simrv::core::pmp::kPmpL | simrv::core::pmp::kPmpModeNa4 | simrv::core::pmp::kPmpR;
    auto res_cfg = cpu.write_csr(0x3A0, cfg_val);
    expect(res_cfg.has_value(), "Write to pmpcfg0 succeeded");

    // Verify written values
    auto read_addr = cpu.read_csr(0x3B0);
    expect(read_addr.has_value() && *read_addr == test_addr, "Read pmpaddr0 matches written value");
    auto read_cfg = cpu.read_csr(0x3A0);
    expect(read_cfg.has_value() && (*read_cfg & 0xFF) == cfg_val,
           "Read pmpcfg0 matches written value with L=1");

    // Attempt to write locked pmpaddr0 - should be ignored
    (void)cpu.write_csr(0x3B0, static_cast<CSRValue>(0x90000000 >> 2));
    read_addr = cpu.read_csr(0x3B0);
    expect(read_addr.has_value() && *read_addr == test_addr,
           "Locked pmpaddr0 ignored subsequent write");

    // Attempt to write locked pmpcfg0 - should be ignored
    (void)cpu.write_csr(0x3A0, 0);
    read_cfg = cpu.read_csr(0x3A0);
    expect(read_cfg.has_value() && (*read_cfg & 0xFF) == cfg_val,
           "Locked pmpcfg0 ignored subsequent write");
}

void test_pmp_mmu_page_walk() {
    std::array<Byte, 64 * 1024> memory{};
    const Address dram_base = 0x80000000;
    const Address dram_size = memory.size();
    simrv::Mmu mmu(memory.data(), dram_base, dram_size);

    simrv::core::ArchState state{};
    state.priv = PrivilegeLevel::Supervisor;

    const Word root_ppn = dram_base >> 12;
    Word satp = 0;
    if constexpr (simrv::xlen::kIsXLen64) {
        satp = (8ULL << 60) | root_ppn;  // Sv39
    } else {
        satp = (1ULL << 31) | root_ppn;  // Sv32
    }

    // Configure PMP to DENY read on page table memory at 0x80000000 (TOR [0x80000000, 0x80001000)
    // no R)
    state.pmpaddr[0] = static_cast<Address>(dram_base >> 2);
    state.pmpcfg[0] = simrv::core::pmp::kPmpModeTor;  // [0, dram_base) no perms

    state.pmpaddr[1] = static_cast<Address>((dram_base + 0x1000) >> 2);
    state.pmpcfg[1] = simrv::core::pmp::kPmpModeTor;  // [dram_base, dram_base + 0x1000) no R/W/X
    state.refresh_pmp_status();

    // Attempt page walk: Reading PTE at 0x80000000 should fail PMP check and return Access Fault
    // (FaultLoad)
    auto result = mmu.translate(0x10000, simrv::PteAccess::Read, PrivilegeLevel::Supervisor, 0,
                                satp, simrv::xlen::kXLenBits, true, &state);
    expect(!result.has_value(), "Page walk across PMP-prohibited PTE fails");
    if (!result.has_value()) {
        expect(result.error() == enum_mask(ExceptionCode::FaultLoad),
               "PMP failure during page walk raises Load Access Fault (FaultLoad)");
    }
}

}  // namespace

auto main() -> int {
    std::cout << "Running PmpTests...\n";
    test_pmp_matching_modes();
    test_pmp_partial_overlap();
    test_pmp_machine_mode_and_locks();
    test_pmp_csr_locking_and_updates();
    test_pmp_mmu_page_walk();

    if (failures != 0) {
        std::cerr << failures << " test(s) failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "All PmpTests passed successfully!\n";
    return EXIT_SUCCESS;
}
