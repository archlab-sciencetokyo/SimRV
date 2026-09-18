/**
 * @file RvCompTests.cpp
 * @brief Regression tests for RVComp cycle-accurate CPU model profile and microarchitecture.
 */

#include <cstdint>
#include <cstdlib>
#include <iostream>

#include "simrv/core/Cpu.hpp"
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
    std::cout << "[Test] CpuModelProfile::RvComp validation & config...\n";
    const auto profile = simrv::pipeline::make_cpu_model_profile(CpuModelProfile::RvComp);
    const auto valid = profile.validate();
    TEST_CHECK(valid.has_value());

    TEST_CHECK(profile.profile == CpuModelProfile::RvComp);
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

    TEST_CHECK(profile.instruction_cache.capacity_bytes == 16384);
    TEST_CHECK(profile.instruction_cache.associativity == 1);
    TEST_CHECK(profile.instruction_cache.line_bytes == 32);
    TEST_CHECK(profile.data_cache.capacity_bytes == 16384);
    TEST_CHECK(profile.data_cache.associativity == 1);
    TEST_CHECK(profile.data_cache.line_bytes == 32);
    TEST_CHECK(profile.data_cache.hit_latency == 4);

    TEST_CHECK(simrv::pipeline::parse_cpu_model_profile("rvcomp") == CpuModelProfile::RvComp);
    TEST_CHECK(simrv::pipeline::parse_cpu_model_profile("rv-comp") == CpuModelProfile::RvComp);
    TEST_CHECK(simrv::pipeline::cpu_model_profile_name(CpuModelProfile::RvComp) == "rvcomp");

    const CSRValue ima_bits = simrv::isa::misa_profile_bits(MisaProfile::IMA);
    const CSRValue ima_misa = simrv::isa::misa_with_mxl(ima_bits);
    if constexpr (!simrv::xlen::kIsXLen64) {
        // In RVComp Verilog (RVComp/src/rvcom.vh): `define ISA_CODE 32'h40141101
        TEST_CHECK(ima_misa == 0x40141101U);
    }
}

void test_rvcomp_machine_application() {
    std::cout << "[Test] CpuModelProfile::RvComp machine configuration...\n";
    simrv::core::Machine machine;
    auto& cpu = machine.primary_hart();
    cpu.machine_ = &machine;
    cpu.reset();

    const auto profile = simrv::pipeline::make_cpu_model_profile(CpuModelProfile::RvComp);
    cpu.apply_cpu_model_config(profile);

    TEST_CHECK(cpu.cpu_model_config.profile == CpuModelProfile::RvComp);
    TEST_CHECK(cpu.pipeline_sim.config.branch_mispredict_penalty == 4);
    TEST_CHECK(cpu.pipeline_sim.config.mul_latency == 2);
    TEST_CHECK(cpu.pipeline_sim.config.div_latency == 34);

    if constexpr (!simrv::xlen::kIsXLen64) {
        TEST_CHECK(cpu.state().misa == 0x40141101U);
    }
}

int main() {
    std::cout << "=== Running RVComp Profile Tests ===" << std::endl;
    test_rvcomp_profile_validation();
    test_rvcomp_machine_application();
    std::cout << "All RVComp profile tests passed successfully." << std::endl;
    return 0;
}
