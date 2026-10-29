/**
 * @file ModernSimulatorStandardsTests.cpp
 * @brief Regression tests for Privileged ISA 1.13, SBI v3.0, timing domains, and simulation stats.
 */
#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/PlatformTiming.hpp"
#include "simrv/core/SbiTypes.hpp"
#include "simrv/core/SimulationStats.hpp"
#include "simrv/debug/SymbolTable.hpp"
#include "simrv/isa/TrapTypes.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/PageTableWalker.hpp"

namespace {

#define TEST_ASSERT(cond)                                                                    \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__ << ": " #cond \
                      << std::endl;                                                          \
            std::exit(1);                                                                    \
        }                                                                                    \
    } while (false)

void test_exception_priorities() {
    using namespace simrv::isa;

    // Test priority ranking per Table 3.7
    TEST_ASSERT(exception_priority_rank(ExceptionCode::MisalignedFetch) == 1);
    TEST_ASSERT(exception_priority_rank(ExceptionCode::FaultFetch) == 2);
    TEST_ASSERT(exception_priority_rank(ExceptionCode::FetchPageFault) == 2);
    TEST_ASSERT(exception_priority_rank(ExceptionCode::IllegalInstruction) == 3);
    TEST_ASSERT(exception_priority_rank(ExceptionCode::Breakpoint) == 4);
    TEST_ASSERT(exception_priority_rank(ExceptionCode::UserEcall) == 4);
    TEST_ASSERT(exception_priority_rank(ExceptionCode::SupervisorEcall) == 4);
    TEST_ASSERT(exception_priority_rank(ExceptionCode::MachineEcall) == 4);
    TEST_ASSERT(exception_priority_rank(ExceptionCode::MisalignedStore) == 5);
    TEST_ASSERT(exception_priority_rank(ExceptionCode::MisalignedLoad) == 5);
    TEST_ASSERT(exception_priority_rank(ExceptionCode::FaultStore) == 6);
    TEST_ASSERT(exception_priority_rank(ExceptionCode::StorePageFault) == 6);
    TEST_ASSERT(exception_priority_rank(ExceptionCode::FaultLoad) == 6);
    TEST_ASSERT(exception_priority_rank(ExceptionCode::LoadPageFault) == 6);
    TEST_ASSERT(exception_priority_rank(ExceptionCode::SoftwareCheck) == 7);
    TEST_ASSERT(exception_priority_rank(ExceptionCode::HardwareError) == 7);

    // Test priority selection
    TEST_ASSERT(higher_priority_exception(ExceptionCode::MisalignedFetch,
                                          ExceptionCode::IllegalInstruction) ==
                ExceptionCode::MisalignedFetch);
    TEST_ASSERT(
        higher_priority_exception(ExceptionCode::IllegalInstruction, ExceptionCode::Breakpoint) ==
        ExceptionCode::IllegalInstruction);
    TEST_ASSERT(
        higher_priority_exception(ExceptionCode::Breakpoint, ExceptionCode::StorePageFault) ==
        ExceptionCode::Breakpoint);
    TEST_ASSERT(
        higher_priority_exception(ExceptionCode::MisalignedLoad, ExceptionCode::LoadPageFault) ==
        ExceptionCode::MisalignedLoad);
}

void test_svpbmt_svnapot() {
    using namespace simrv::memory;

    // Standard PMA PTE
    simrv::PteView pte_pma{.raw = 0xCF};
    TEST_ASSERT(pte_pma.pbmt() == PtePbmt::Pma);
    TEST_ASSERT(!pte_pma.is_napot());
    TEST_ASSERT(!pte_pma.has_reserved_bits(simrv::xlen::kIsXLen64 ? 8 : 4));

    if constexpr (simrv::xlen::kIsXLen64) {
        // Svpbmt NonCacheable
        simrv::PteView pte_nc;
        pte_nc.raw = static_cast<simrv::xlen::Word>((1ULL << 61U) | 0xCF);
        TEST_ASSERT(pte_nc.pbmt() == PtePbmt::NonCacheable);
        TEST_ASSERT(!pte_nc.has_reserved_bits(8));

        // Svpbmt IO
        simrv::PteView pte_io;
        pte_io.raw = static_cast<simrv::xlen::Word>((2ULL << 61U) | 0xCF);
        TEST_ASSERT(pte_io.pbmt() == PtePbmt::Io);
        TEST_ASSERT(!pte_io.has_reserved_bits(8));

        // Svpbmt Reserved (PBMT=3)
        simrv::PteView pte_res;
        pte_res.raw = static_cast<simrv::xlen::Word>((3ULL << 61U) | 0xCF);
        TEST_ASSERT(pte_res.pbmt() == PtePbmt::Reserved);
        TEST_ASSERT(pte_res.has_reserved_bits(8));

        // Svnapot bit 63
        simrv::PteView pte_napot;
        pte_napot.raw = static_cast<simrv::xlen::Word>((1ULL << 63U) | 0xCF);
        TEST_ASSERT(pte_napot.is_napot());
        TEST_ASSERT(!pte_napot.has_reserved_bits(8));

        // Reserved bit 55 set
        simrv::PteView pte_bad_res;
        pte_bad_res.raw = static_cast<simrv::xlen::Word>((1ULL << 55U) | 0xCF);
        TEST_ASSERT(pte_bad_res.has_reserved_bits(8));
    }
}

void test_hypervisor_sv39x4_guest_translation() {
#if SIMRV_XLEN == 64
    using simrv::memory::PteAccess;
    constexpr simrv::xlen::Word kRoot = 0x4000;
    constexpr simrv::xlen::Word kMiddle = 0x9000;
    constexpr simrv::xlen::Word kLeaf = 0xA000;
    constexpr simrv::xlen::Address kGuestAddress = 0x12345678;
    constexpr simrv::xlen::Address kPhysicalPage = 0x70000;
    std::array<simrv::xlen::Byte, 1U << 20U> backing{};
    const auto store_pte = [&](simrv::xlen::Address address, uint64_t value) {
        std::memcpy(backing.data() + address, &value, sizeof(value));
    };
    const auto load_pte = [&](simrv::xlen::Address address) {
        uint64_t value = 0;
        std::memcpy(&value, backing.data() + address, sizeof(value));
        return value;
    };
    const auto root_index = (kGuestAddress >> 30U) & 0x7FFU;
    const auto middle_index = (kGuestAddress >> 21U) & 0x1FFU;
    const auto leaf_index = (kGuestAddress >> 12U) & 0x1FFU;
    store_pte(kRoot + root_index * 8U, (uint64_t{9} << 10U) | 1U);
    store_pte(kMiddle + middle_index * 8U, (uint64_t{10} << 10U) | 1U);
    constexpr uint64_t kValidReadWriteAccessedDirty = 1U | 2U | 4U | 0x40U | 0x80U;
    const auto leaf_address = kLeaf + leaf_index * 8U;
    store_pte(leaf_address, ((kPhysicalPage >> 12U) << 10U) | kValidReadWriteAccessedDirty);

    simrv::memory::PageTableWalker walker(backing.data(), 0, backing.size());
    const simrv::xlen::Word hgatp = (uint64_t{8} << 60U) | (kRoot >> 12U);
    const auto translated = walker.walk_guest_physical(kGuestAddress, PteAccess::Read, hgatp, 64);
    TEST_ASSERT(translated.has_value());
    TEST_ASSERT(translated->physical_address.raw() == kPhysicalPage + (kGuestAddress & 0xFFFU));

    const auto write_translated =
        walker.walk_guest_physical(kGuestAddress, PteAccess::Write, hgatp, 64);
    TEST_ASSERT(write_translated.has_value());

    store_pte(leaf_address, load_pte(leaf_address) & ~uint64_t{0x80});
    const auto dirty_fault = walker.walk_guest_physical(kGuestAddress, PteAccess::Write, hgatp, 64);
    TEST_ASSERT(!dirty_fault.has_value());
    TEST_ASSERT(dirty_fault.error() == simrv::isa::ExceptionCode::GuestStorePageFault);

    const auto out_of_range =
        walker.walk_guest_physical(uint64_t{1} << 41U, PteAccess::Read, hgatp, 64);
    TEST_ASSERT(!out_of_range.has_value());
    TEST_ASSERT(out_of_range.error() == simrv::isa::ExceptionCode::GuestLoadPageFault);

    const auto bare = walker.walk_guest_physical(kGuestAddress, PteAccess::Read, 0, 64);
    TEST_ASSERT(bare.has_value() && bare->physical_address.raw() == kGuestAddress);
#endif
}

void test_sbi_version_and_rfence() {
    TEST_ASSERT(simrv::sbi::kSbiSpecVersion_3_0 == 0x03000000U);

    simrv::core::CPU cpu;
    cpu.state().regs.write(simrv::xlen::RegId::A7, 0x10);  // Extension: Base
    cpu.state().regs.write(simrv::xlen::RegId::A6, 0x0);   // Function: GetSpecVersion

    const bool handled = cpu.sbi.handle_ecall(simrv::isa::ExceptionCode::SupervisorEcall);
    TEST_ASSERT(handled);
    TEST_ASSERT(cpu.state().regs.read(simrv::xlen::RegId::A0) == 0);  // SBI_SUCCESS
    TEST_ASSERT(cpu.state().regs.read(simrv::xlen::RegId::A1) == simrv::sbi::kSbiSpecVersion_3_0);

    // Test RFNC RemoteSfenceVmaAsid
    cpu.state().regs.write(simrv::xlen::RegId::A7, 0x52464E43);  // RFNC
    cpu.state().regs.write(simrv::xlen::RegId::A6, 2);           // RemoteSfenceVmaAsid
    cpu.state().regs.write(simrv::xlen::RegId::A0, 1);           // Hart mask: hart 0
    cpu.state().regs.write(simrv::xlen::RegId::A1, 0);           // Hart mask base: 0
    cpu.state().regs.write(simrv::xlen::RegId::A2, 0x1000);      // start_addr
    cpu.state().regs.write(simrv::xlen::RegId::A3, 4096);        // size
    cpu.state().regs.write(simrv::xlen::RegId::A4, 42);          // ASID 42

    const bool rfence_handled = cpu.sbi.handle_ecall(simrv::isa::ExceptionCode::SupervisorEcall);
    TEST_ASSERT(rfence_handled);
    TEST_ASSERT(cpu.state().regs.read(simrv::xlen::RegId::A0) == 0);  // SBI_SUCCESS
}

void test_sbi_pmu_discovery_and_raw_counters() {
    using simrv::xlen::RegId;
    simrv::core::CPU cpu;
    cpu.state().regs.write(RegId::A7, std::to_underlying(simrv::sbi::SbiExtId::Pmu));
    const auto call = [&](simrv::xlen::Word fid) {
        cpu.state().regs.write(RegId::A6, fid);
        TEST_ASSERT(cpu.sbi.handle_ecall(simrv::isa::ExceptionCode::SupervisorEcall));
        return std::pair{static_cast<simrv::xlen::SignedWord>(cpu.state().regs.read(RegId::A0)),
                         cpu.state().regs.read(RegId::A1)};
    };

    TEST_ASSERT(call(0).second == 32);
    cpu.state().regs.write(RegId::A0, 0);
    TEST_ASSERT(call(1).second == (0xC00U | (63U << 12U)));
    cpu.state().regs.write(RegId::A0, 1);
    TEST_ASSERT(call(1).first == static_cast<simrv::xlen::SignedWord>(simrv::SbiError::InvalidParam));

    // Configure SimRV raw selector 9 on programmable counter 3.
    cpu.state().regs.write(RegId::A0, 3);
    cpu.state().regs.write(RegId::A1, 1);
    cpu.state().regs.write(RegId::A2, 0);
    cpu.state().regs.write(RegId::A3, 0x30000);  // raw-event-v2, event code 0
    cpu.state().regs.write(RegId::A4, 9);
    const auto configured = call(2);
    TEST_ASSERT(configured.first == 0 && configured.second == 3);
    cpu.state().regs.write(RegId::A0, 3);
    cpu.state().regs.write(RegId::A1, 1);
    cpu.state().regs.write(RegId::A2, 1);  // set initial counter value
    cpu.state().regs.write(RegId::A3, 40);
    TEST_ASSERT(call(3).first == 0);
    TEST_ASSERT(cpu.perf_counters.read_hpmcounter(3) == 40);
    cpu.perf_counters.notify_event(simrv::core::HpmEvent::BranchMispredict);
    TEST_ASSERT(cpu.perf_counters.read_hpmcounter(3) == 41);
    cpu.state().regs.write(RegId::A0, 3);
    cpu.state().regs.write(RegId::A1, 1);
    cpu.state().regs.write(RegId::A2, 0);
    TEST_ASSERT(call(4).first == 0);
    cpu.perf_counters.notify_event(simrv::core::HpmEvent::BranchMispredict);
    TEST_ASSERT(cpu.perf_counters.read_hpmcounter(3) == 41);

}

void test_platform_timing_conversions() {
    using namespace simrv::core::timing;

    // 160 MHz clock: 1 second = 160,000,000 cycles = 1,000,000,000 ns
    const auto ns = cycles_to_nanoseconds(160'000'000ULL, 160'000'000ULL);
    TEST_ASSERT(ns == 1'000'000'000ULL);

    const auto cycles = nanoseconds_to_cycles(1'000'000'000ULL, 160'000'000ULL);
    TEST_ASSERT(cycles == 160'000'000ULL);

    // 10 MHz timebase: 10,000,000 ticks = 160,000,000 cycles
    const auto ticks_cycles =
        timebase_ticks_to_cycles(10'000'000ULL, 160'000'000ULL, 10'000'000ULL);
    TEST_ASSERT(ticks_cycles == 160'000'000ULL);
}

void test_simulation_stats() {
    simrv::core::Machine machine;
    const auto snapshot = simrv::core::take_stats_snapshot(machine);

    TEST_ASSERT(snapshot.harts.size() == machine.num_harts());
    TEST_ASSERT(snapshot.total_retired_instructions == machine.retired_instruction_count());

    const std::string json_str = snapshot.to_json();
    TEST_ASSERT(!json_str.empty());
    TEST_ASSERT(json_str.find("\"total_retired_instructions\"") != std::string::npos);
    TEST_ASSERT(json_str.find("\"harts\"") != std::string::npos);

    const std::string text_str = snapshot.to_string();
    TEST_ASSERT(!text_str.empty());
    TEST_ASSERT(text_str.find("=== SimRV Simulation Statistics ===") != std::string::npos);
}

void test_architectural_signature_dump() {
    simrv::core::Machine machine;
    std::array<simrv::xlen::Byte, 4096> backing{};
    machine.set_ram_for_testing(backing.data(), backing.size());
    machine.memory().initialize_mmu();

    const std::string sig_path = "/tmp/simrv_test_signature.sig";

    // 1. Missing symbols fails cleanly
    TEST_ASSERT(!machine.dump_signature(sig_path));

    // 2. Add signature symbols
    const simrv::xlen::Address begin_sig = 0x80000100;
    const simrv::xlen::Address end_sig = 0x80000110;  // 16 bytes = 4 words
    machine.symbol_table().add_symbol_for_testing(begin_sig, "begin_signature");
    machine.symbol_table().add_symbol_for_testing(end_sig, "end_signature");

    // Populate RAM at begin_sig (offset 0x100 within DRAM base 0x80000000)
    const uint32_t words[4] = {0x12345678, 0xdeadbeef, 0x00000001, 0xaabbccdd};
    std::memcpy(backing.data() + 0x100, words, sizeof(words));

    // Dump signature
    TEST_ASSERT(machine.dump_signature(sig_path));

    // Read back and verify format
    std::ifstream in(sig_path);
    TEST_ASSERT(in.is_open());
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(in, line)) {
        if (!line.empty()) lines.push_back(line);
    }
    in.close();
    std::filesystem::remove(sig_path);

    TEST_ASSERT(lines.size() == 4);
    TEST_ASSERT(lines[0] == "12345678");
    TEST_ASSERT(lines[1] == "deadbeef");
    TEST_ASSERT(lines[2] == "00000001");
    TEST_ASSERT(lines[3] == "aabbccdd");
}

}  // namespace

int main() {
    std::cout << "Running ModernSimulatorStandardsTests..." << std::endl;
    test_exception_priorities();
    test_svpbmt_svnapot();
    test_hypervisor_sv39x4_guest_translation();
    test_sbi_version_and_rfence();
    test_sbi_pmu_discovery_and_raw_counters();
    test_platform_timing_conversions();
    test_simulation_stats();
    test_architectural_signature_dump();
    std::cout << "All ModernSimulatorStandardsTests passed successfully!" << std::endl;
    return 0;
}
