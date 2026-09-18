/**
 * @file CfuProvingGroundTests.cpp
 * @brief Unit and pipeline timing tests for CFU-ProvingGround preset and Custom Function Unit.
 */
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "simrv/cache/BaseCache.hpp"
#include "simrv/cache/ICache.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/RuntimeProfile.hpp"
#include "simrv/execute/CfuUnit.hpp"
#include "simrv/isa/Base.hpp"
#include "simrv/isa/OperationId.hpp"
#include "simrv/pipeline/CpuModel.hpp"
#include "simrv/pipeline/Decoder.hpp"
#include "simrv/pipeline/OperationTraits.hpp"
#include "simrv/pipeline/PipelineConfig.hpp"
#include "simrv/pipeline/PipelineSim.hpp"
#include "simrv/util/CliParser.hpp"
#include "simrv/xlen/Types.hpp"

#define TEST_CHECK(expr)                                                                     \
    do {                                                                                     \
        if (!(expr)) {                                                                       \
            std::cerr << "Assertion failed: " #expr " at " __FILE__ ":" << __LINE__ << "\n"; \
            std::abort();                                                                    \
        }                                                                                    \
    } while (0)

namespace {

using simrv::execute::CfuUnit;
using simrv::isa::Opcode;
using simrv::isa::OperationId;
using simrv::pipeline::BranchPredictorType;
using simrv::pipeline::CpuModelProfile;
using simrv::pipeline::PipelineType;

void test_cfu_provingground_profile() {
    std::cout << "[Test] CpuModelProfile::CfuProvingGround validation & config...\n";

    const auto profile = simrv::pipeline::make_cpu_model_profile(CpuModelProfile::CfuProvingGround);
    TEST_CHECK(profile.validate().has_value());

    // 5-stage in-order core
    TEST_CHECK(profile.pipeline.pipeline_type == PipelineType::FiveStage);
    TEST_CHECK(profile.pipeline.enable_forwarding);
    TEST_CHECK(profile.pipeline.mul_latency == 2);
    TEST_CHECK(profile.pipeline.div_latency == 34);
    TEST_CHECK(profile.pipeline.branch_mispredict_penalty == 3);
    TEST_CHECK(profile.pipeline.cycle_counter_start_delay == 2);

    // Bimodal branch predictor (2048 BTB entries, 2048 BHT entries, no RAS)
    TEST_CHECK(profile.pipeline.branch_predictor.type == BranchPredictorType::Bimodal);
    TEST_CHECK(profile.pipeline.branch_predictor.btb_entries == 2048);
    TEST_CHECK(profile.pipeline.branch_predictor.bht_entries == 2048);
    TEST_CHECK(!profile.pipeline.branch_predictor.enable_ras);
    TEST_CHECK(profile.pipeline.branch_predictor.registered_btb_read);
    TEST_CHECK(profile.pipeline.branch_predictor.bht_initial_state == 0);

    // On-chip BRAM memory models (32 KiB IMEM, 16 KiB DMEM, 1-cycle hit latency)
    TEST_CHECK(profile.instruction_cache.capacity_bytes == 32768);
    TEST_CHECK(profile.instruction_cache.associativity == 1);
    TEST_CHECK(profile.instruction_cache.hit_latency == 1);
    TEST_CHECK(profile.data_cache.capacity_bytes == 16384);
    TEST_CHECK(profile.data_cache.associativity == 1);
    TEST_CHECK(profile.data_cache.hit_latency == 1);

    // Profile string parsing and names
    TEST_CHECK(simrv::pipeline::parse_cpu_model_profile("cfu-provingground") ==
               CpuModelProfile::CfuProvingGround);
    TEST_CHECK(simrv::pipeline::parse_cpu_model_profile("rvproc") ==
               CpuModelProfile::CfuProvingGround);
    TEST_CHECK(simrv::pipeline::cpu_model_profile_name(CpuModelProfile::CfuProvingGround) ==
               "cfu-provingground");
}

void test_cfu_instruction_decode_and_traits() {
    std::cout << "[Test] CFU instruction decoding and operation traits...\n";

    // Build raw CUSTOM_0 instruction:
    // funct7 = 0x05, rs2 = 3 (gp), rs1 = 2 (sp), funct3 = 0x04, rd = 1 (ra), opcode = 0x0B
    constexpr uint32_t raw =
        (0x05u << 25) | (3u << 20) | (2u << 15) | (4u << 12) | (1u << 7) | 0x0Bu;
    const auto op = simrv::pipeline::decoder(raw);
    TEST_CHECK(op == OperationId::CFU);

    const simrv::pipeline::Decoder d(raw);
    TEST_CHECK(d.opcode() == Opcode::Custom0);
    TEST_CHECK(static_cast<uint32_t>(d.funct3()) == 4);
    TEST_CHECK(static_cast<uint32_t>(d.funct7()) == 5);
    TEST_CHECK(d.rd() == RegId::Ra);
    TEST_CHECK(d.rs1() == RegId::Sp);
    TEST_CHECK(d.rs2() == RegId::Gp);

    // Traits
    const auto traits =
        simrv::pipeline::operation::make_dependency_traits(op, Opcode::Custom0, RegId::Ra, 0);
    TEST_CHECK(traits.is_cfu);
    TEST_CHECK(traits.writes_int);
    TEST_CHECK(traits.reads_rs1_int);
    TEST_CHECK(traits.reads_rs2_int);

    // Namespace helpers
    TEST_CHECK(simrv::pipeline::operation::is_cfu(OperationId::CFU));
    TEST_CHECK(simrv::pipeline::operation::writes_integer(OperationId::CFU));
    TEST_CHECK(simrv::pipeline::operation::is_rs1_int(OperationId::CFU));
    TEST_CHECK(simrv::pipeline::operation::is_rs2_int(OperationId::CFU));
    TEST_CHECK(!simrv::pipeline::operation::is_load(OperationId::CFU));
    TEST_CHECK(!simrv::pipeline::operation::is_store(OperationId::CFU));
    TEST_CHECK(simrv::pipeline::operation_name(OperationId::CFU) == "CFU");
}

void test_cfu_unit_default_execution() {
    std::cout << "[Test] Default CFU execution (bitwise OR)...\n";

    CfuUnit cfu;
    TEST_CHECK(!cfu.has_plugin());
    TEST_CHECK(cfu.plugin_path().empty());

    // Default operation matches cfu.v: rslt = src1 | src2
    const uint32_t res1 = cfu.execute(0, 0, 0x1200, 0x0034);
    TEST_CHECK(res1 == 0x1234);
    TEST_CHECK(cfu.last_latency() == 1);
    TEST_CHECK(cfu.query_latency(0, 0) == 1);

    const uint32_t res2 = cfu.execute(0x7f, 0x07, 0xdead0000, 0x0000beef);
    TEST_CHECK(res2 == 0xdeadbeef);
    TEST_CHECK(cfu.last_latency() == 1);
}

void test_cfu_unit_plugin_loading() {
    std::cout << "[Test] Dynamic CFU plugin loading...\n";

    CfuUnit cfu;
    TEST_CHECK(!cfu.load_plugin("/nonexistent/path/libcfu_nonexistent.so"));
    TEST_CHECK(!cfu.has_plugin());

#ifdef TEST_CFU_PLUGIN_PATH
    TEST_CHECK(cfu.load_plugin(TEST_CFU_PLUGIN_PATH));
    TEST_CHECK(cfu.has_plugin());
    TEST_CHECK(!cfu.plugin_path().empty());

    // Test custom operation: (src1 ^ src2) + funct7; funct3 == 1 -> latency 3
    const uint32_t res1 = cfu.execute(10, 0, 0x10, 0x01);
    TEST_CHECK(res1 == ((0x10 ^ 0x01) + 10));
    TEST_CHECK(cfu.last_latency() == 1);

    const uint32_t res2 = cfu.execute(5, 1, 0xaa, 0x55);
    TEST_CHECK(res2 == ((0xaa ^ 0x55) + 5));
    TEST_CHECK(cfu.last_latency() == 3);

    TEST_CHECK(cfu.query_latency(0, 1) == 3);
    TEST_CHECK(cfu.query_latency(0, 0) == 1);

    // Reset should maintain plugin
    cfu.reset();
    TEST_CHECK(cfu.has_plugin());
#endif
}

void test_cfu_pipeline_hazard_timing() {
    std::cout << "[Test] CFU pipeline hazard stalls & 1-cycle latency modeling...\n";

    // Test program:
    // 0: addi x1, x0, 10           (0x00a00093)
    // 1: addi x2, x0, 20           (0x01400113)
    // 2: custom0 x3, x1, x2 (OR)   (0x0020818b) -> x3 = 10 | 20 = 30
    // 3: addi x4, x3, 5 (RAW dep)  (0x00518213) -> x4 = 30 + 5 = 35
    // 4: jal x0, 0                 (0x0000006f)
    constexpr std::array<Instruction, 5> program_dependent = {
        0x00a00093,  // addi x1, x0, 10
        0x01400113,  // addi x2, x0, 20
        0x0020818b,  // custom0 x3, x1, x2 (funct7=0, funct3=0)
        0x00518213,  // addi x4, x3, 5 (depends on x3)
        0x0000006f,  // jal x0, 0
    };

    // 1. Back-to-back dependent instruction:
    // In CFU-ProvingGround (RVProc), CFU output is registered at EX/MEM.
    // Back-to-back dependency causes a 1-cycle Decode hazard stall.
    {
        simrv::core::Machine machine;
        const Address pc = machine.memory_geometry().dram_base;
        std::vector<Byte> ram(1024 * 1024, Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());

        auto& cpu = machine.primary_hart();
        cpu.machine_ = &machine;
        cpu.reset();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        const auto profile =
            simrv::pipeline::make_cpu_model_profile(CpuModelProfile::CfuProvingGround);
        cpu.pipeline_sim.config = profile.pipeline;
        cpu.cpu_model_config = profile;

        std::array<Byte, simrv::cache::ICache::kLineBytes> line{};
        std::memcpy(line.data(), program_dependent.data(), sizeof(program_dependent));
        std::memcpy(ram.data(), program_dependent.data(), sizeof(program_dependent));
        cpu.icache.insert(pc, line.data(), simrv::memory::MesiState::Exclusive);
        cpu.state().pc = pc;

        uint32_t cycles = 0;
        while (cpu.e_icount < 4 && cycles < 64) {
            cpu.run_cycle(machine);
            machine.memory().system_bus().advance_cycle();
            ++cycles;
        }

        TEST_CHECK(cpu.e_icount == 4);
        TEST_CHECK(cpu.state().regs.read(RegId::Ra) == 10);
        TEST_CHECK(cpu.state().regs.read(RegId::Sp) == 20);
        TEST_CHECK(cpu.state().regs.read(RegId::Gp) == 30);  // 10 | 20
        TEST_CHECK(cpu.state().regs.read(RegId::Tp) == 35);  // 30 + 5
        TEST_CHECK(cpu.pipeline_sim.data_hazard_stalls() == 1);
    }

    // 2. Independent instruction in between:
    // Inserting an independent instruction eliminates the 1-cycle hazard stall!
    // 0: addi x1, x0, 10
    // 1: addi x2, x0, 20
    // 2: custom0 x3, x1, x2 (OR)
    // 3: addi x5, x0, 99 (independent)
    // 4: addi x4, x3, 5 (depends on x3)
    // 5: jal x0, 0
    constexpr std::array<Instruction, 6> program_independent = {
        0x00a00093,  // addi x1, x0, 10
        0x01400113,  // addi x2, x0, 20
        0x0020818b,  // custom0 x3, x1, x2
        0x06300293,  // addi x5, x0, 99 (independent)
        0x00518213,  // addi x4, x3, 5 (depends on x3)
        0x0000006f,  // jal x0, 0
    };

    {
        simrv::core::Machine machine;
        const Address pc = machine.memory_geometry().dram_base;
        std::vector<Byte> ram(1024 * 1024, Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());

        auto& cpu = machine.primary_hart();
        cpu.machine_ = &machine;
        cpu.reset();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        const auto profile =
            simrv::pipeline::make_cpu_model_profile(CpuModelProfile::CfuProvingGround);
        cpu.pipeline_sim.config = profile.pipeline;
        cpu.cpu_model_config = profile;

        std::array<Byte, simrv::cache::ICache::kLineBytes> line{};
        std::memcpy(line.data(), program_independent.data(), sizeof(program_independent));
        std::memcpy(ram.data(), program_independent.data(), sizeof(program_independent));
        cpu.icache.insert(pc, line.data(), simrv::memory::MesiState::Exclusive);
        cpu.state().pc = pc;

        uint32_t cycles = 0;
        while (cpu.e_icount < 5 && cycles < 64) {
            cpu.run_cycle(machine);
            machine.memory().system_bus().advance_cycle();
            ++cycles;
        }

        TEST_CHECK(cpu.e_icount == 5);
        TEST_CHECK(cpu.state().regs.read(RegId::Gp) == 30);
        TEST_CHECK(cpu.state().regs.read(RegId::T0) == 99);
        TEST_CHECK(cpu.state().regs.read(RegId::Tp) == 35);
        TEST_CHECK(cpu.pipeline_sim.data_hazard_stalls() == 0);
    }
}

void test_bram_prewarm_and_cli() {
    std::cout << "[Test] BRAM pre-warm CLI options and cache population...\n";

    // 1. CLI default for cfu-provingground: bram_prewarm is true
    {
        std::array<char*, 4> argv = {const_cast<char*>("SimRV"), const_cast<char*>("--tui"),
                                     const_cast<char*>("--cpu-profile"),
                                     const_cast<char*>("cfu-provingground")};
        const auto res = simrv::util::parse_command_line(argv);
        TEST_CHECK(res.has_value());
        const auto cfg = res->options.to_machine_config();
        TEST_CHECK(cfg.bram_prewarm);
    }

    // Explicit predictor overrides survive machine initialization/profile application.
    {
        std::array<char*, 6> argv = {
            const_cast<char*>("SimRV"),         const_cast<char*>("--tui"),
            const_cast<char*>("--cpu-profile"), const_cast<char*>("cfu-provingground"),
            const_cast<char*>("--bpred"),       const_cast<char*>("none")};
        const auto res = simrv::util::parse_command_line(argv);
        TEST_CHECK(res.has_value());
        const auto cfg = res->options.to_machine_config();
        TEST_CHECK(cfg.branch_predictor_type == BranchPredictorType::Disabled);
    }

    // 2. CLI explicit override: --no-bram-prewarm
    {
        std::array<char*, 5> argv = {const_cast<char*>("SimRV"), const_cast<char*>("--tui"),
                                     const_cast<char*>("--cpu-profile"),
                                     const_cast<char*>("cfu-provingground"),
                                     const_cast<char*>("--no-bram-prewarm")};
        const auto res = simrv::util::parse_command_line(argv);
        TEST_CHECK(res.has_value());
        const auto cfg = res->options.to_machine_config();
        TEST_CHECK(!cfg.bram_prewarm);
    }

    // 3. CLI explicit flag: --bram-prewarm for other profiles
    {
        std::array<char*, 5> argv = {const_cast<char*>("SimRV"), const_cast<char*>("--tui"),
                                     const_cast<char*>("--cpu-profile"),
                                     const_cast<char*>("balanced"),
                                     const_cast<char*>("--bram-prewarm")};
        const auto res = simrv::util::parse_command_line(argv);
        TEST_CHECK(res.has_value());
        const auto cfg = res->options.to_machine_config();
        TEST_CHECK(cfg.bram_prewarm);
    }

    // 4. Test prewarm_bram_caches populates ICache and DCache without misses
    {
        simrv::core::Machine machine;
        std::vector<Byte> ram(1024 * 1024, Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());

        auto& cpu = machine.primary_hart();
        cpu.machine_ = &machine;
        cpu.reset();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        const auto profile =
            simrv::pipeline::make_cpu_model_profile(CpuModelProfile::CfuProvingGround);
        cpu.pipeline_sim.config = profile.pipeline;
        cpu.cpu_model_config = profile;

        constexpr std::array<Instruction, 3> test_prog = {
            0x00a00093,  // addi x1, x0, 10
            0x01400113,  // addi x2, x0, 20
            0x002081b3,  // add x3, x1, x2
        };
        const Address code_addr = machine.memory_geometry().dram_base;
        std::memcpy(ram.data(), test_prog.data(), sizeof(test_prog));

        constexpr uint32_t data_val = 0x12345678;
        const Address data_addr = machine.memory_geometry().dram_base + 0x1000;
        std::memcpy(ram.data() + (data_addr - machine.memory_geometry().dram_base), &data_val,
                    sizeof(data_val));

        machine.clear_loaded_segments();
        machine.record_loaded_segment(code_addr, sizeof(test_prog), true);
        machine.record_loaded_segment(data_addr, sizeof(data_val), false);

        machine.prewarm_bram_caches();

        // Verify ICache hit on the prewarmed code line
        uint32_t fetched_inst = 0;
        TEST_CHECK(cpu.icache.read(code_addr, fetched_inst));
        TEST_CHECK(fetched_inst == test_prog[0]);
        TEST_CHECK(cpu.icache.miss_count() == 0);

        // Verify DCache hit on the prewarmed data line
        Word read_data = 0;
        TEST_CHECK(cpu.dcache.read(data_addr, read_data,
                                   static_cast<Instruction>(simrv::isa::Funct3::Lw)));
        TEST_CHECK(static_cast<uint32_t>(read_data) == data_val);
        TEST_CHECK(cpu.dcache.miss_count() == 0);
    }
}

}  // namespace

auto main() -> int {
    std::cout << "=== Running CFU-ProvingGround Tests ===\n";
    test_cfu_provingground_profile();
    test_cfu_instruction_decode_and_traits();
    test_cfu_unit_default_execution();
    test_cfu_unit_plugin_loading();
    test_cfu_pipeline_hazard_timing();
    test_bram_prewarm_and_cli();
    std::cout << "=== All CFU-ProvingGround Tests Passed Successfully ===\n";
    return 0;
}
