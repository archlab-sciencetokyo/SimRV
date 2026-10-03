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
#include "simrv/execute/ExecuteUnit.hpp"
#include "simrv/isa/Base.hpp"
#include "simrv/isa/Common.hpp"
#include "simrv/isa/Zb.hpp"
#include "simrv/isa/Zk.hpp"
#include "simrv/pipeline/CpuModel.hpp"

#define TEST_CHECK(cond)                                                                  \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ \
                      << std::endl;                                                       \
            std::exit(1);                                                                 \
        }                                                                                 \
    } while (0)

using simrv::isa::IsaPreset;
using simrv::pipeline::BranchPredictorType;
using simrv::pipeline::CpuModelPreset;
using simrv::pipeline::PipelineType;

void test_resolve_cpu_model_path() {
    std::cout << "[Test] resolve_cpu_model_path...\n";
    const auto rvcomp_res = simrv::core::resolve_cpu_model_path("rvcomp");
    TEST_CHECK(rvcomp_res.has_value());
    TEST_CHECK(rvcomp_res->ends_with("rvcomp.cfg"));

    const auto cfu_res = simrv::core::resolve_cpu_model_path("cfu-provingground");
    TEST_CHECK(cfu_res.has_value());
    TEST_CHECK(cfu_res->ends_with("cfu-provingground.cfg"));

    const auto rv32e_res = simrv::core::resolve_cpu_model_path("rv32e");
    TEST_CHECK(rv32e_res.has_value());
    TEST_CHECK(rv32e_res->ends_with("rv32e.cfg"));

    const auto direct_res = simrv::core::resolve_cpu_model_path("configs/models/rvcomp.cfg");
    TEST_CHECK(direct_res.has_value());

    // Generic presets are compiled-in defaults, not config files
    const auto balanced_res = simrv::core::resolve_cpu_model_path("balanced");
    TEST_CHECK(!balanced_res.has_value());
    TEST_CHECK(simrv::pipeline::parse_cpu_model_preset("balanced") == CpuModelPreset::Balanced);

    const auto nonexistent = simrv::core::resolve_cpu_model_path("non_existent_core_profile_xyz");
    TEST_CHECK(!nonexistent.has_value());
}

void test_load_canonical_rvcomp_cfg() {
    std::cout << "[Test] Loading configs/models/rvcomp.cfg...\n";
    const auto path = simrv::core::resolve_cpu_model_path("rvcomp");
    TEST_CHECK(path.has_value());

    simrv::pipeline::CpuModelConfig config{};
    const bool ok = simrv::core::parse_cpu_config(*path, config);
    TEST_CHECK(ok);

    TEST_CHECK(config.name == "rvcomp");
    TEST_CHECK(config.supported_xlen == 32);
    TEST_CHECK(config.preset == CpuModelPreset::Custom);
    TEST_CHECK(config.isa_preset == IsaPreset::IMA);

    simrv::pipeline::CpuModelConfig loaded{};
    TEST_CHECK(simrv::core::load_cpu_config(*path, loaded));
    TEST_CHECK(config.validate().has_value());

    TEST_CHECK(config.pipeline.pipeline_type == PipelineType::FiveStage);
    TEST_CHECK(config.pipeline.enable_forwarding == true);
    TEST_CHECK(config.pipeline.mul_latency == 2);
    TEST_CHECK(config.pipeline.div_latency == 35);
    TEST_CHECK(config.pipeline.branch_mispredict_penalty == 4);
    TEST_CHECK(config.pipeline.host_interface_latency == 21);
    TEST_CHECK(config.pipeline.host_interface_phase_period == 2);

    const auto& bp = config.pipeline.branch_predictor;
    TEST_CHECK(bp.type == BranchPredictorType::Bimodal);
    TEST_CHECK(bp.btb_entries == 512);
    TEST_CHECK(bp.bht_entries == 8192);
    TEST_CHECK(bp.pc_shift == 2);
    TEST_CHECK(bp.untagged_btb == true);
    TEST_CHECK(bp.predict_non_control == true);
    TEST_CHECK(bp.jump_uses_direction_counter == false);
    TEST_CHECK(bp.jump_uses_current_btb == false);
    TEST_CHECK(bp.registered_btb_read == false);
    TEST_CHECK(bp.bht_initial_state == 1);

    TEST_CHECK(config.instruction_cache.capacity_bytes == 16384);
    TEST_CHECK(config.instruction_cache.associativity == 1);
    TEST_CHECK(config.instruction_cache.line_bytes == 32);
    TEST_CHECK(config.instruction_cache.hit_latency == 1);
    TEST_CHECK(config.instruction_cache.miss_latency == 1);
    TEST_CHECK(config.instruction_front_cache.capacity_bytes == 1024);
    TEST_CHECK(config.instruction_front_cache.line_bytes == 16);
    TEST_CHECK(config.instruction_front_cache.freeze_pipeline_on_refill);
    TEST_CHECK(config.interconnect.startup_data_response_latency == 27);

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
    const bool ok = simrv::core::parse_cpu_config(*path, config);
    TEST_CHECK(ok);

    TEST_CHECK(config.name == "cfu-provingground");
    TEST_CHECK(config.supported_xlen == 32);
    TEST_CHECK(config.preset == CpuModelPreset::Custom);
    TEST_CHECK(config.isa_preset == IsaPreset::IM);
    TEST_CHECK(config.isa_extensions.size() == 1);
    TEST_CHECK(config.has_isa_extension("x-cfu"));
    TEST_CHECK(config.cfu.enabled);
    TEST_CHECK(config.cfu.opcode == "custom-0");
    TEST_CHECK(config.cfu.default_latency == 1);
    TEST_CHECK(config.cfu.rtl_module == "rvproc_cfu");

    simrv::pipeline::CpuModelConfig loaded{};
    TEST_CHECK(simrv::core::load_cpu_config(*path, loaded));
    TEST_CHECK(config.validate().has_value());
    TEST_CHECK(config.pipeline.pipeline_type == PipelineType::FiveStage);
    TEST_CHECK(config.pipeline.mul_latency == 3);
    TEST_CHECK(config.pipeline.div_latency == 36);
    TEST_CHECK(config.pipeline.branch_mispredict_penalty == 3);
    TEST_CHECK(config.pipeline.cycle_counter_start_delay == 2);

    const auto& bp = config.pipeline.branch_predictor;
    TEST_CHECK(bp.type == BranchPredictorType::Bimodal);
    TEST_CHECK(bp.btb_entries == 2048);
    TEST_CHECK(bp.bht_entries == 2048);
    TEST_CHECK(bp.predict_non_control == true);
    TEST_CHECK(bp.jump_uses_direction_counter == true);
    TEST_CHECK(bp.jump_uses_current_btb == true);
    TEST_CHECK(bp.registered_btb_read == true);
    TEST_CHECK(bp.bht_initial_state == 0);
}

void test_multi_letter_isa_extensions() {
    std::cout << "[Test] Parsing multi-letter ISA extensions and Zk shorthand...\n";
    simrv::pipeline::CpuModelConfig config{};
    TEST_CHECK(simrv::core::parse_cpu_config_string(
        "[cpu]\nname = \"crypto-test\"\nmisa = \"rv64im_zkn\"\n\n"
        "[isa]\nextensions = \"zkr,zkt\"\n",
        config));
    TEST_CHECK(config.isa_preset == IsaPreset::IM);
    TEST_CHECK(config.has_isa_extension("zkn"));
    TEST_CHECK(config.has_isa_extension("zkr"));
    TEST_CHECK(config.has_isa_extension("zkt"));

    simrv::pipeline::CpuModelConfig shorthand{};
    TEST_CHECK(simrv::core::parse_cpu_config_string(
        "[cpu]\nmisa = \"rv64im\"\n[isa]\nextensions = \"zk\"\n", shorthand));
    TEST_CHECK(shorthand.has_isa_extension("zkn"));
    TEST_CHECK(shorthand.has_isa_extension("zkr"));
    TEST_CHECK(shorthand.has_isa_extension("zkt"));
    TEST_CHECK(simrv::isa::is_zkn_operation(simrv::isa::OperationId::CLMUL));
    TEST_CHECK(simrv::isa::is_zkn_operation(simrv::isa::OperationId::PACK));
    TEST_CHECK(simrv::isa::is_zb_operation(simrv::isa::OperationId::ROL));
    TEST_CHECK(simrv::isa::is_zb_crypto_operation(simrv::isa::OperationId::CLMUL));
    TEST_CHECK(!simrv::isa::is_zkn_operation(simrv::isa::OperationId::ROL));
    using simrv::execute::ExecuteUnit;
    using simrv::isa::OperationId;
    TEST_CHECK(ExecuteUnit::aluInt(0x12, 0x34, OperationId::CLMUL, 32) == 0x328);
    TEST_CHECK(ExecuteUnit::aluInt(0x1122, 0x3344, OperationId::PACK, 32) == 0x33441122);
}

void test_rv32e_profile() {
    std::cout << "[Test] Parsing and validating RV32E profiles...\n";
    simrv::pipeline::CpuModelConfig config{};
    TEST_CHECK(simrv::core::parse_cpu_config_string(
        "[cpu]\nname = \"rv32e-test\"\nmisa = \"rv32e\"\n", config));
    TEST_CHECK(config.isa_preset == IsaPreset::E);
    TEST_CHECK(config.supported_xlen == 32);
    TEST_CHECK(config.validate().has_value());

    const auto e = simrv::isa::isa_preset_bits(IsaPreset::E);
    TEST_CHECK(simrv::isa::misa_has_extension(e, simrv::isa::IsaExtension::E));
    TEST_CHECK(!simrv::isa::misa_has_extension(e, simrv::isa::IsaExtension::I));
    TEST_CHECK(simrv::isa::instruction_enabled_by_misa(e, simrv::isa::OperationId::ADD));

    simrv::pipeline::CpuModelConfig extended{};
    TEST_CHECK(simrv::core::parse_cpu_config_string(
        "[cpu]\nname = \"rv32e-zba\"\nmisa = \"e_zba\"\n", extended));
    TEST_CHECK(extended.isa_preset == IsaPreset::E);
    TEST_CHECK(extended.supported_xlen == 32);
    TEST_CHECK(extended.has_isa_extension("zba"));

    const auto em = simrv::isa::isa_preset_bits(IsaPreset::EM);
    TEST_CHECK(simrv::isa::misa_has_extension(em, simrv::isa::IsaExtension::E));
    TEST_CHECK(simrv::isa::misa_has_extension(em, simrv::isa::IsaExtension::M));
    TEST_CHECK(!simrv::isa::misa_has_extension(em, simrv::isa::IsaExtension::I));
    const auto emac = simrv::isa::isa_preset_bits(IsaPreset::EMAC);
    TEST_CHECK(simrv::isa::misa_has_extension(emac, simrv::isa::IsaExtension::A));
    TEST_CHECK(simrv::isa::misa_has_extension(emac, simrv::isa::IsaExtension::C));

    simrv::pipeline::CpuModelConfig preset{};
    TEST_CHECK(simrv::core::parse_cpu_config(*simrv::core::resolve_cpu_model_path("rv32e"),
                                              preset));
    TEST_CHECK(preset.name == "rv32e");
    TEST_CHECK(preset.supported_xlen == 32);
    TEST_CHECK(preset.pipeline.pipeline_type == PipelineType::ThreeStage);

    using simrv::RegId;
    TEST_CHECK(simrv::isa::rv32e_register_is_valid(RegId::A5));
    TEST_CHECK(!simrv::isa::rv32e_register_is_valid(RegId::A6));
    TEST_CHECK(simrv::isa::rv32e_register_operands_valid(
        true, RegId::A5, true, RegId::A4, true, RegId::A3));
    TEST_CHECK(!simrv::isa::rv32e_register_operands_valid(
        true, RegId::A6, true, RegId::A4, true, RegId::A3));
    TEST_CHECK(!simrv::isa::rv32e_register_operands_valid(
        true, RegId::A5, true, RegId::A6, true, RegId::A3));
    TEST_CHECK(!simrv::isa::rv32e_register_operands_valid(
        true, RegId::A5, true, RegId::A4, true, RegId::A6));
    TEST_CHECK(simrv::isa::rv32e_register_operands_valid(
        false, RegId::A6, false, RegId::A6, false, RegId::A6));
}

void test_serialize_and_roundtrip() {
    std::cout << "[Test] Serialize & round-trip...\n";
    auto original = simrv::pipeline::make_cpu_model_preset(CpuModelPreset::Performance);
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
    TEST_CHECK(text.find("misa = \"gcbv\"") != std::string::npos);

    simrv::pipeline::CpuModelConfig reloaded{};
    const bool ok = simrv::core::load_cpu_config_string(text, reloaded);
    TEST_CHECK(ok);
    TEST_CHECK(reloaded.isa_preset == IsaPreset::GCBV);
    TEST_CHECK(reloaded.pipeline.mul_latency == 5);
    TEST_CHECK(reloaded.pipeline.div_latency == 22);
    TEST_CHECK(reloaded.pipeline.branch_mispredict_penalty == 6);
    TEST_CHECK(reloaded.pipeline.branch_predictor.bht_initial_state == 2);
}

void test_save_cpu_config_file() {
    std::cout << "[Test] File save & reload...\n";
    const auto temp_dir = std::filesystem::temp_directory_path();
    const auto temp_file = temp_dir / "simrv_test_model_save.cfg";

    auto original = simrv::pipeline::make_cpu_model_preset(CpuModelPreset::Tiny);
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

void test_xlen_compatibility_rules() {
    std::cout << "[Test] test_xlen_compatibility_rules...\n";
    auto base = simrv::pipeline::make_cpu_model_preset(CpuModelPreset::Tiny);

    // 1. Any model with supported_xlen = 0 is neutral and validates on both RV32 and RV64
    base.supported_xlen = 0;
    TEST_CHECK(base.validate().has_value());

    // 2. 32-bit model validates on both RV32 and RV64 builds
    base.supported_xlen = 32;
    TEST_CHECK(base.validate().has_value());

    // 3. 64-bit model validates on RV64, but is rejected on RV32 build
    base.supported_xlen = 64;
    if constexpr (!simrv::xlen::kIsXLen64) {
        const auto res = base.validate();
        TEST_CHECK(!res.has_value());
        TEST_CHECK(res.error().find("requires XLEN=64") != std::string::npos);
    } else {
        TEST_CHECK(base.validate().has_value());
    }

    // 4. Invalid XLEN values are rejected on all builds
    base.supported_xlen = 16;
    const auto err16 = base.validate();
    TEST_CHECK(!err16.has_value());
    TEST_CHECK(err16.error().find("specifies unsupported XLEN=16") != std::string::npos);
}

int main() {
    test_resolve_cpu_model_path();
    test_load_canonical_rvcomp_cfg();
    test_load_canonical_cfu_provingground_cfg();
    test_multi_letter_isa_extensions();
    test_rv32e_profile();
    test_serialize_and_roundtrip();
    test_save_cpu_config_file();
    test_xlen_compatibility_rules();
    std::cout << "All CpuModelLoader tests passed!\n";
    return 0;
}
