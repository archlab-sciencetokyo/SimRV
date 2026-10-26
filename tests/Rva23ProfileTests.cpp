/**
 * @file Rva23ProfileTests.cpp
 * @brief Regression tests for RVA23 Profile: Zicond, CMO (Zicboz/Zicbom/Zicbop), Svinval, and
 * profile engine.
 */
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <vector>

#include "simrv/cache/DCache.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/execute/ExecuteUnit.hpp"
#include "simrv/isa/IsaConfig.hpp"
#include "simrv/isa/IsaTypes.hpp"
#include "simrv/isa/OperationId.hpp"
#include "simrv/memory/MemoryAccess.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/pipeline/Decoder.hpp"
#include "simrv/xlen/AddressTypes.hpp"
#include "simrv/xlen/RegisterTypes.hpp"

namespace {

#define TEST_ASSERT(cond)                                                                    \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__ << ": " #cond \
                      << std::endl;                                                          \
            std::exit(1);                                                                    \
        }                                                                                    \
    } while (false)

void test_zicond_semantics() {
    using namespace simrv::isa;
    using namespace simrv::execute;

    // 1. czero.eqz rd, rs1, rs2: rd = (rs2 == 0) ? 0 : rs1
    // rs2 == 0 -> rd = 0
    TEST_ASSERT(ExecuteUnit::aluInt(42, 0, OperationId::CZERO_EQZ, simrv::xlen::kXLenBits) == 0);
    // rs2 != 0 -> rd = rs1
    TEST_ASSERT(ExecuteUnit::aluInt(42, 1, OperationId::CZERO_EQZ, simrv::xlen::kXLenBits) == 42);
    TEST_ASSERT(ExecuteUnit::aluInt(0x12345678, 0xABCDEF, OperationId::CZERO_EQZ,
                                    simrv::xlen::kXLenBits) == 0x12345678);

    // 2. czero.nez rd, rs1, rs2: rd = (rs2 != 0) ? 0 : rs1
    // rs2 == 0 -> rd = rs1
    TEST_ASSERT(ExecuteUnit::aluInt(42, 0, OperationId::CZERO_NEZ, simrv::xlen::kXLenBits) == 42);
    // rs2 != 0 -> rd = 0
    TEST_ASSERT(ExecuteUnit::aluInt(42, 1, OperationId::CZERO_NEZ, simrv::xlen::kXLenBits) == 0);
    TEST_ASSERT(ExecuteUnit::aluInt(0x12345678, 0xABCDEF, OperationId::CZERO_NEZ,
                                    simrv::xlen::kXLenBits) == 0);

    // 3. Decoder verification
    // czero.eqz x3, x1, x2 -> funct7=0x07, rs2=2, rs1=1, funct3=5, rd=3, opcode=0x33
    const uint32_t czero_eqz_insn =
        (0x07U << 25U) | (2U << 20U) | (1U << 15U) | (5U << 12U) | (3U << 7U) | 0x33U;
    TEST_ASSERT(simrv::pipeline::decoder(czero_eqz_insn) == OperationId::CZERO_EQZ);

    // czero.nez x3, x1, x2 -> funct7=0x07, rs2=2, rs1=1, funct3=7, rd=3, opcode=0x33
    const uint32_t czero_nez_insn =
        (0x07U << 25U) | (2U << 20U) | (1U << 15U) | (7U << 12U) | (3U << 7U) | 0x33U;
    TEST_ASSERT(simrv::pipeline::decoder(czero_nez_insn) == OperationId::CZERO_NEZ);
}

void test_cmo_decoder_and_predicates() {
    using namespace simrv::isa;

    // cbo.clean (rs1=x1) -> funct12=0x001, rs1=1, funct3=2, rd=0, opcode=0x0F
    const uint32_t cbo_clean_insn = (0x001U << 20U) | (1U << 15U) | (2U << 12U) | 0x0FU;
    TEST_ASSERT(simrv::pipeline::decoder(cbo_clean_insn) == OperationId::CBO_CLEAN);
    TEST_ASSERT(is_cmo_op(OperationId::CBO_CLEAN));

    // cbo.flush (rs1=x1) -> funct12=0x002, rs1=1, funct3=2, rd=0, opcode=0x0F
    const uint32_t cbo_flush_insn = (0x002U << 20U) | (1U << 15U) | (2U << 12U) | 0x0FU;
    TEST_ASSERT(simrv::pipeline::decoder(cbo_flush_insn) == OperationId::CBO_FLUSH);
    TEST_ASSERT(is_cmo_op(OperationId::CBO_FLUSH));

    // cbo.inval (rs1=x1) -> funct12=0x000, rs1=1, funct3=2, rd=0, opcode=0x0F
    const uint32_t cbo_inval_insn = (0x000U << 20U) | (1U << 15U) | (2U << 12U) | 0x0FU;
    TEST_ASSERT(simrv::pipeline::decoder(cbo_inval_insn) == OperationId::CBO_INVAL);
    TEST_ASSERT(is_cmo_op(OperationId::CBO_INVAL));

    // cbo.zero (rs1=x1) -> funct12=0x004, rs1=1, funct3=2, rd=0, opcode=0x0F
    const uint32_t cbo_zero_insn = (0x004U << 20U) | (1U << 15U) | (2U << 12U) | 0x0FU;
    TEST_ASSERT(simrv::pipeline::decoder(cbo_zero_insn) == OperationId::CBO_ZERO);
    TEST_ASSERT(is_cmo_op(OperationId::CBO_ZERO));

    // prefetch.r (offset=0, rs1=x1) -> funct5=1, funct3=6, rd=0, opcode=0x13
    const uint32_t prefetch_r_insn = (0x01U << 20U) | (1U << 15U) | (6U << 12U) | 0x13U;
    TEST_ASSERT(simrv::pipeline::decoder(prefetch_r_insn) == OperationId::PREFETCH_R);
    TEST_ASSERT(is_cmo_op(OperationId::PREFETCH_R));

    // prefetch.w (offset=0, rs1=x1) -> funct5=3, funct3=6, rd=0, opcode=0x13
    const uint32_t prefetch_w_insn = (0x03U << 20U) | (1U << 15U) | (6U << 12U) | 0x13U;
    TEST_ASSERT(simrv::pipeline::decoder(prefetch_w_insn) == OperationId::PREFETCH_W);
    TEST_ASSERT(is_cmo_op(OperationId::PREFETCH_W));

    // prefetch.i (offset=0, rs1=x1) -> funct5=0, funct3=6, rd=0, opcode=0x13
    const uint32_t prefetch_i_insn = (0x00U << 20U) | (1U << 15U) | (6U << 12U) | 0x13U;
    TEST_ASSERT(simrv::pipeline::decoder(prefetch_i_insn) == OperationId::PREFETCH_I);
    TEST_ASSERT(is_cmo_op(OperationId::PREFETCH_I));
}

void test_svinval_decoder() {
    using namespace simrv::isa;

    // sinval.vma rs1=x1, rs2=x2 -> funct7=0x0B, rs2=2, rs1=1, funct3=0, rd=0, opcode=0x73
    const uint32_t sinval_vma_insn = (0x0BU << 25U) | (2U << 20U) | (1U << 15U) | 0x73U;
    TEST_ASSERT(simrv::pipeline::decoder(sinval_vma_insn) == OperationId::SINVAL_VMA);

    // sfence.w.inval -> funct12=0x180, rs1=0, funct3=0, rd=0, opcode=0x73
    const uint32_t sfence_w_inval_insn = (0x180U << 20U) | 0x73U;
    TEST_ASSERT(simrv::pipeline::decoder(sfence_w_inval_insn) == OperationId::SFENCE_W_INVAL);

    // sfence.inval.ir -> funct12=0x181, rs1=0, funct3=0, rd=0, opcode=0x73
    const uint32_t sfence_inval_ir_insn = (0x181U << 20U) | 0x73U;
    TEST_ASSERT(simrv::pipeline::decoder(sfence_inval_ir_insn) == OperationId::SFENCE_INVAL_IR);
}

void test_cbo_zero_execution() {
    using namespace simrv::isa;
    using namespace simrv::core;

    Machine machine;
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0xAA});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.memory().initialize_mmu();

    auto& cpu = machine.primary_hart();
    cpu.reset();

    constexpr size_t kLineBytes = simrv::cache::DCache::kLineBytes;
    constexpr simrv::xlen::Address target_paddr = simrv::memory::kDramBaseAddress + 0x1000;
    // Verify target buffer is filled with 0xAA
    const auto* ram_ptr = machine.ram_view().unchecked_ptr(target_paddr);
    for (size_t i = 0; i < kLineBytes; ++i) {
        TEST_ASSERT(ram_ptr[i] == static_cast<simrv::xlen::Byte>(0xAA));
    }

    // Execute cbo.zero on target_paddr
    simrv::memory::MemoryAccess::execute_cmo(
        machine.memory(), cpu, simrv::xlen::VirtAddr{target_paddr + 16}, OperationId::CBO_ZERO);

    // Check no pending exception
    TEST_ASSERT(!cpu.active_context().pending_exception.has_value());

    // Verify all bytes of cache block are zeroed
    for (size_t i = 0; i < kLineBytes; ++i) {
        TEST_ASSERT(ram_ptr[i] == static_cast<simrv::xlen::Byte>(0));
    }
    // Verify byte right after block is still 0xAA
    TEST_ASSERT(ram_ptr[kLineBytes] == static_cast<simrv::xlen::Byte>(0xAA));

    // Test permission check: if menvcfg.CBZE = 0, supervisor cbo.zero must trap
    cpu.state().priv = kPrivSupervisor;
    cpu.state().menvcfg &= ~(1ULL << 6);  // Clear CBZE
    simrv::memory::MemoryAccess::execute_cmo(
        machine.memory(), cpu, simrv::xlen::VirtAddr{target_paddr}, OperationId::CBO_ZERO);
    TEST_ASSERT(cpu.active_context().pending_exception == ExceptionCode::IllegalInstruction);
}

void test_profile_engine() {
    using namespace simrv::isa;

    // 1. String conversion
    TEST_ASSERT(to_profile_string(ArchitectureProfile::Rva23S64) == "rva23s64");
    TEST_ASSERT(to_profile_string(ArchitectureProfile::Rva23U64) == "rva23u64");
    TEST_ASSERT(to_profile_string(ArchitectureProfile::Legacy) == "legacy");

    // 2. String parsing
    TEST_ASSERT(parse_architecture_profile("rva23") == ArchitectureProfile::Rva23S64);
    TEST_ASSERT(parse_architecture_profile("rva23s64") == ArchitectureProfile::Rva23S64);
    TEST_ASSERT(parse_architecture_profile("rva23u64") == ArchitectureProfile::Rva23U64);
    TEST_ASSERT(parse_architecture_profile("rva22") == ArchitectureProfile::Rva22S64);
    TEST_ASSERT(parse_architecture_profile("legacy") == ArchitectureProfile::Legacy);
    TEST_ASSERT(parse_architecture_profile("rv64gc") == ArchitectureProfile::Legacy);
    TEST_ASSERT(parse_architecture_profile("invalid_profile") == std::nullopt);

    // 3. Default profile configuration
    if constexpr (simrv::xlen::kIsXLen64) {
        TEST_ASSERT(kDefaultArchitectureProfile == ArchitectureProfile::Rva23S64);
    } else {
        TEST_ASSERT(kDefaultArchitectureProfile == ArchitectureProfile::Legacy);
    }
}

}  // namespace

int main() {
    std::cout << "[RUN] test_zicond_semantics" << std::endl;
    test_zicond_semantics();
    std::cout << "[PASS] test_zicond_semantics" << std::endl;

    std::cout << "[RUN] test_cmo_decoder_and_predicates" << std::endl;
    test_cmo_decoder_and_predicates();
    std::cout << "[PASS] test_cmo_decoder_and_predicates" << std::endl;

    std::cout << "[RUN] test_svinval_decoder" << std::endl;
    test_svinval_decoder();
    std::cout << "[PASS] test_svinval_decoder" << std::endl;

    std::cout << "[RUN] test_cbo_zero_execution" << std::endl;
    test_cbo_zero_execution();
    std::cout << "[PASS] test_cbo_zero_execution" << std::endl;

    std::cout << "[RUN] test_profile_engine" << std::endl;
    test_profile_engine();
    std::cout << "[PASS] test_profile_engine" << std::endl;

    std::cout << "\nAll RVA23 Profile unit tests passed successfully!\n";
    return 0;
}
