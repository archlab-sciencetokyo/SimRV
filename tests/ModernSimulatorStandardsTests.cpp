/**
 * @file ModernSimulatorStandardsTests.cpp
 * @brief Regression tests for Privileged ISA 1.13, SBI v3.0, timing domains, and simulation stats.
 */
#include <cassert>
#include <iostream>
#include <string>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/PlatformTiming.hpp"
#include "simrv/core/SbiTypes.hpp"
#include "simrv/core/SimulationStats.hpp"
#include "simrv/isa/TrapTypes.hpp"
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

}  // namespace

int main() {
    std::cout << "Running ModernSimulatorStandardsTests..." << std::endl;
    test_exception_priorities();
    test_svpbmt_svnapot();
    test_sbi_version_and_rfence();
    test_platform_timing_conversions();
    test_simulation_stats();
    std::cout << "All ModernSimulatorStandardsTests passed successfully!" << std::endl;
    return 0;
}
