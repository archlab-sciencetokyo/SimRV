/**
 * @file RvCompTests.cpp
 * @brief Regression tests for RVComp cycle-accurate CPU model configuration and microarchitecture.
 */

#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/CpuConfigParser.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/MachineConfig.hpp"
#include "simrv/isa/Base.hpp"
#include "simrv/isa/Common.hpp"
#include "simrv/pipeline/CpuModel.hpp"
#include "simrv/xlen/Types.hpp"

#define TEST_CHECK(cond)                                                                  \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ \
                      << std::endl;                                                       \
            std::exit(1);                                                                 \
        }                                                                                 \
    } while (0)

using simrv::isa::MisaProfile;
using simrv::pipeline::BranchPredictorType;
using simrv::pipeline::CpuModelProfile;
using simrv::pipeline::PipelineType;

void test_rvcomp_profile_validation() {
    std::cout << "[Test] RVComp config from configs/models/rvcomp.cfg...\n";
    const auto path = simrv::core::resolve_cpu_model_path("rvcomp");
    TEST_CHECK(path.has_value());

    simrv::pipeline::CpuModelConfig profile{};
    TEST_CHECK(simrv::core::parse_cpu_config(*path, profile));

    TEST_CHECK(profile.name == "rvcomp");
    TEST_CHECK(profile.supported_xlen == 32);
    TEST_CHECK(profile.misa_profile == MisaProfile::IMA);
    TEST_CHECK(profile.pipeline.pipeline_type == PipelineType::FiveStage);
    TEST_CHECK(profile.pipeline.enable_forwarding == true);
    TEST_CHECK(profile.pipeline.mul_latency == 2);
    TEST_CHECK(profile.pipeline.div_latency == 34);
    TEST_CHECK(profile.pipeline.branch_mispredict_penalty == 4);
    TEST_CHECK(profile.pipeline.cycle_counter_start_delay == 0);

    TEST_CHECK(profile.pipeline.branch_predictor.type == BranchPredictorType::Bimodal);
    TEST_CHECK(profile.pipeline.branch_predictor.btb_entries == 512);
    TEST_CHECK(profile.pipeline.branch_predictor.bht_entries == 8192);
    TEST_CHECK(profile.pipeline.branch_predictor.enable_ras == false);
    TEST_CHECK(profile.pipeline.branch_predictor.pc_shift == 2);
    TEST_CHECK(profile.pipeline.branch_predictor.untagged_btb == true);
    TEST_CHECK(profile.pipeline.branch_predictor.registered_btb_read == false);
    TEST_CHECK(profile.pipeline.branch_predictor.bht_initial_state == 1);

    TEST_CHECK(profile.instruction_cache.capacity_bytes == 1024);
    TEST_CHECK(profile.instruction_cache.associativity == 1);
    TEST_CHECK(profile.instruction_cache.line_bytes == 32);
    TEST_CHECK(profile.instruction_cache.miss_latency == 64);
    TEST_CHECK(profile.data_cache.capacity_bytes == 16384);
    TEST_CHECK(profile.data_cache.associativity == 1);
    TEST_CHECK(profile.data_cache.line_bytes == 32);
    TEST_CHECK(profile.data_cache.hit_latency == 4);

    simrv::pipeline::CpuModelConfig loaded{};
    TEST_CHECK(simrv::core::load_cpu_config(*path, loaded));
    TEST_CHECK(profile.validate().has_value());
    const CSRValue ima_bits = simrv::isa::misa_profile_bits(MisaProfile::IMA);
    const CSRValue ima_misa = simrv::isa::misa_with_mxl(ima_bits, 32);
    // In RVComp Verilog (RVComp/src/rvcom.vh): `define ISA_CODE 32'h40141101
    if constexpr (!simrv::xlen::kIsXLen64) {
        TEST_CHECK(ima_misa == 0x40141101U);
    } else {
        TEST_CHECK(ima_misa == ((1ull << 62) | 0x00141101ULL));
    }

    // A model requiring XLEN=64 must be rejected on an RV32 build
    simrv::pipeline::CpuModelConfig model64 = profile;
    model64.name = "mock64";
    model64.supported_xlen = 64;
    if constexpr (!simrv::xlen::kIsXLen64) {
        const auto valid = model64.validate();
        TEST_CHECK(!valid.has_value());
        TEST_CHECK(valid.error().find("requires XLEN=64") != std::string::npos);
    } else {
        TEST_CHECK(model64.validate().has_value());
    }
}

void test_rvcomp_machine_application() {
    std::cout << "[Test] RVComp machine configuration...\n";
    const auto path = simrv::core::resolve_cpu_model_path("rvcomp");
    TEST_CHECK(path.has_value());

    simrv::pipeline::CpuModelConfig profile{};
    TEST_CHECK(simrv::core::load_cpu_config(*path, profile));

    simrv::core::Machine machine;
    auto& cpu = machine.primary_hart();
    cpu.machine_ = &machine;
    cpu.reset();

    cpu.apply_cpu_model_config(profile);

    TEST_CHECK(cpu.cpu_model_config.name == "rvcomp");
    TEST_CHECK(cpu.pipeline_sim.config.branch_mispredict_penalty == 4);
    TEST_CHECK(cpu.pipeline_sim.config.mul_latency == 2);
    TEST_CHECK(cpu.pipeline_sim.config.div_latency == 34);
    TEST_CHECK(cpu.state().regs.xlen == 32);
    if constexpr (!simrv::xlen::kIsXLen64) {
        TEST_CHECK(cpu.state().misa == 0x40141101U);
    } else {
        TEST_CHECK(cpu.state().misa == ((1ull << 62) | 0x00141101ULL));
    }
}

int main() {
    std::cout << "=== Running RVComp Profile Tests ===" << std::endl;
    test_rvcomp_profile_validation();
    test_rvcomp_machine_application();
    std::cout << "All RVComp profile tests passed successfully." << std::endl;
    return 0;
}
