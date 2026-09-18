/**
 * @file CpuModelLoaderTests.cpp
 * @brief Regression tests for human-editable CPU model (.cfg) parser, serializer, and resolver.
 */

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>

#include "simrv/core/CpuConfigParser.hpp"
#include "simrv/isa/Base.hpp"
#include "simrv/pipeline/CpuModel.hpp"

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

void test_resolve_cpu_model_path() {
    std::cout << "[Test] resolve_cpu_model_path...\n";
    const auto rvcomp_res = simrv::core::resolve_cpu_model_path("rvcomp");
    TEST_CHECK(rvcomp_res.has_value());
    TEST_CHECK(rvcomp_res->ends_with("rvcomp.cfg"));

    const auto balanced_res = simrv::core::resolve_cpu_model_path("balanced");
    TEST_CHECK(balanced_res.has_value());
    TEST_CHECK(balanced_res->ends_with("balanced.cfg"));

    const auto direct_res = simrv::core::resolve_cpu_model_path("configs/models/tiny.cfg");
    TEST_CHECK(direct_res.has_value());

    const auto nonexistent = simrv::core::resolve_cpu_model_path("non_existent_core_profile_xyz");
    TEST_CHECK(!nonexistent.has_value());
}

void test_load_canonical_rvcomp_cfg() {
    std::cout << "[Test] Loading configs/models/rvcomp.cfg...\n";
    const auto path = simrv::core::resolve_cpu_model_path("rvcomp");
    TEST_CHECK(path.has_value());

    simrv::pipeline::CpuModelConfig config{};
    const bool ok = simrv::core::load_cpu_config(*path, config);
    TEST_CHECK(ok);

    const auto valid = config.validate();
    TEST_CHECK(valid.has_value());

    TEST_CHECK(config.profile == CpuModelProfile::RvComp);
    TEST_CHECK(config.misa_profile == MisaProfile::IMA);
    TEST_CHECK(config.pipeline.pipeline_type == PipelineType::FiveStage);
    TEST_CHECK(config.pipeline.enable_forwarding == true);
    TEST_CHECK(config.pipeline.mul_latency == 2);
    TEST_CHECK(config.pipeline.div_latency == 34);
    TEST_CHECK(config.pipeline.branch_mispredict_penalty == 4);

    const auto& bp = config.pipeline.branch_predictor;
    TEST_CHECK(bp.type == BranchPredictorType::Bimodal);
    TEST_CHECK(bp.btb_entries == 512);
    TEST_CHECK(bp.bht_entries == 8192);
    TEST_CHECK(bp.pc_shift == 2);
    TEST_CHECK(bp.untagged_btb == true);
    TEST_CHECK(bp.registered_btb_read == false);
    TEST_CHECK(bp.bht_initial_state == 1);

    TEST_CHECK(config.instruction_cache.capacity_bytes == 16384);
    TEST_CHECK(config.instruction_cache.associativity == 1);
    TEST_CHECK(config.instruction_cache.line_bytes == 32);
    TEST_CHECK(config.instruction_cache.hit_latency == 1);

    TEST_CHECK(config.data_cache.capacity_bytes == 16384);
    TEST_CHECK(config.data_cache.associativity == 1);
    TEST_CHECK(config.data_cache.line_bytes == 32);
    TEST_CHECK(config.data_cache.hit_latency == 4);
}

void test_load_canonical_cfu_provingground_cfg() {
    std::cout << "[Test] Loading configs/models/cfu-provingground.cfg...\n";
    const auto path = simrv::core::resolve_cpu_model_path("cfu-provingground");
    TEST_CHECK(path.has_value());

    simrv::pipeline::CpuModelConfig config{};
    const bool ok = simrv::core::load_cpu_config(*path, config);
    TEST_CHECK(ok);

    TEST_CHECK(config.profile == CpuModelProfile::CfuProvingGround);
    TEST_CHECK(config.misa_profile == MisaProfile::IM);
    TEST_CHECK(config.pipeline.pipeline_type == PipelineType::FiveStage);
    TEST_CHECK(config.pipeline.mul_latency == 3);
    TEST_CHECK(config.pipeline.div_latency == 18);
    TEST_CHECK(config.pipeline.branch_mispredict_penalty == 3);
    TEST_CHECK(config.pipeline.cycle_counter_start_delay == 2);

    const auto& bp = config.pipeline.branch_predictor;
    TEST_CHECK(bp.type == BranchPredictorType::Bimodal);
    TEST_CHECK(bp.btb_entries == 2048);
    TEST_CHECK(bp.bht_entries == 2048);
    TEST_CHECK(bp.registered_btb_read == true);
    TEST_CHECK(bp.bht_initial_state == 0);
}

void test_serialize_and_roundtrip() {
    std::cout << "[Test] Serialize & round-trip...\n";
    auto original = simrv::pipeline::make_cpu_model_profile(CpuModelProfile::Performance);
    original.pipeline.mul_latency = 5;
    original.pipeline.div_latency = 22;
    original.pipeline.branch_mispredict_penalty = 6;
    original.pipeline.branch_predictor.bht_initial_state = 2;

    std::ostringstream ss;
    simrv::core::serialize_cpu_config(original, ss, "test_custom_core", "Test custom description");
    const std::string text = ss.str();
    TEST_CHECK(!text.empty());
    TEST_CHECK(text.find("[pipeline]") != std::string::npos);
    TEST_CHECK(text.find("mul_latency = 5") != std::string::npos);
    TEST_CHECK(text.find("div_latency = 22") != std::string::npos);
    TEST_CHECK(text.find("branch_mispredict_penalty = 6") != std::string::npos);
    TEST_CHECK(text.find("bht_initial_state = 2") != std::string::npos);

    simrv::pipeline::CpuModelConfig reloaded{};
    const bool ok = simrv::core::load_cpu_config_string(text, reloaded);
    TEST_CHECK(ok);
    TEST_CHECK(reloaded.pipeline.mul_latency == 5);
    TEST_CHECK(reloaded.pipeline.div_latency == 22);
    TEST_CHECK(reloaded.pipeline.branch_mispredict_penalty == 6);
    TEST_CHECK(reloaded.pipeline.branch_predictor.bht_initial_state == 2);
}

void test_save_cpu_config_file() {
    std::cout << "[Test] File save & reload...\n";
    const auto temp_dir = std::filesystem::temp_directory_path();
    const auto temp_file = temp_dir / "simrv_test_model_save.cfg";

    auto original = simrv::pipeline::make_cpu_model_profile(CpuModelProfile::Tiny);
    original.pipeline.mul_latency = 7;

    const bool saved = simrv::core::save_cpu_config(temp_file.string(), original, "temp_tiny");
    TEST_CHECK(saved);

    simrv::pipeline::CpuModelConfig loaded{};
    const bool ok = simrv::core::load_cpu_config(temp_file.string(), loaded);
    TEST_CHECK(ok);
    TEST_CHECK(loaded.pipeline.mul_latency == 7);
    TEST_CHECK(loaded.pipeline.pipeline_type == PipelineType::ThreeStage);

    std::error_code ec;
    std::filesystem::remove(temp_file, ec);
}

int main() {
    test_resolve_cpu_model_path();
    test_load_canonical_rvcomp_cfg();
    test_load_canonical_cfu_provingground_cfg();
    test_serialize_and_roundtrip();
    test_save_cpu_config_file();
    std::cout << "All CpuModelLoader tests passed!\n";
    return 0;
}
