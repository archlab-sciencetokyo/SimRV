/**
 * @file Scaffolding.cpp
 * @brief Utilities for scaffolding starter CPU models and external device plugins.
 */
#include "simrv/util/Scaffolding.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <print>
#include <sstream>
#include <string>

#include "simrv/core/CpuConfigParser.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/pipeline/CpuModel.hpp"

namespace simrv::util {

auto scaffold_device_plugin(std::string_view name, std::string_view output) -> bool {
    if (name.empty()) {
        simrv::log::error("No device plugin name specified for scaffolding.");
        return false;
    }

    const std::string name_str(name);

    auto generate_c_source = [&name_str]() -> std::string {
        std::ostringstream ss;
        ss << "/**\n"
           << " * @file " << name_str << "_plugin.c\n"
           << " * @brief External device plugin for " << name_str << ".\n"
           << " */\n"
           << "#include <stdint.h>\n"
           << "#include <stdlib.h>\n"
           << "#include \"simrv/device/ExternalDevice.h\"\n\n"
           << "typedef struct {\n"
           << "    const SimRvHostServices* services;\n"
           << "    uint32_t reg_control;\n"
           << "    uint32_t reg_status;\n"
           << "} DeviceState;\n\n"
           << "static int " << name_str << "_init(void** out_ctx, const char* args) {\n"
           << "    (void)args;\n"
           << "    DeviceState* st = (DeviceState*)calloc(1, sizeof(DeviceState));\n"
           << "    if (!st) return -1;\n"
           << "    *out_ctx = st;\n"
           << "    return 0;\n"
           << "}\n\n"
           << "static void " << name_str << "_reset(void* ctx) {\n"
           << "    DeviceState* st = (DeviceState*)ctx;\n"
           << "    if (!st) return;\n"
           << "    st->reg_control = 0;\n"
           << "    st->reg_status = 0;\n"
           << "}\n\n"
           << "static int " << name_str
           << "_read(void* ctx, uint64_t offset, uint32_t size, uint64_t* out_val) {\n"
           << "    DeviceState* st = (DeviceState*)ctx;\n"
           << "    if (!st || !out_val || size != 4) return 0;\n"
           << "    switch (offset) {\n"
           << "        case 0x00: *out_val = st->reg_control; break;\n"
           << "        case 0x04: *out_val = st->reg_status; break;\n"
           << "        default:   *out_val = 0; break;\n"
           << "    }\n"
           << "    return 0;\n"
           << "}\n\n"
           << "static int " << name_str
           << "_write(void* ctx, uint64_t offset, uint32_t size, uint64_t val) {\n"
           << "    DeviceState* st = (DeviceState*)ctx;\n"
           << "    if (!st || size != 4) return 0;\n"
           << "    switch (offset) {\n"
           << "        case 0x00: st->reg_control = (uint32_t)val; break;\n"
           << "        case 0x04: st->reg_status = (uint32_t)val; break;\n"
           << "        default: break;\n"
           << "    }\n"
           << "    return 0;\n"
           << "}\n\n"
           << "static void " << name_str << "_advance_cycles(void* ctx, uint64_t cycles) {\n"
           << "    (void)ctx; (void)cycles;\n"
           << "}\n\n"
           << "static void " << name_str << "_destroy(void* ctx) {\n"
           << "    free(ctx);\n"
           << "}\n\n"
           << "static void " << name_str
           << "_set_services(void* ctx, const SimRvHostServices* services) {\n"
           << "    DeviceState* st = (DeviceState*)ctx;\n"
           << "    if (st) st->services = services;\n"
           << "}\n\n"
           << "static const SimRvExternalDevice kDevice = {\n"
           << "    .api_version = SIMRV_EXTERNAL_DEVICE_API_VERSION,\n"
           << "    .name = \"" << name_str << "\",\n"
           << "    .compatible = \"custom," << name_str << "\",\n"
           << "    .default_base = 0x10050000ULL,\n"
           << "    .default_size = 0x1000ULL,\n"
           << "    .init = " << name_str << "_init,\n"
           << "    .reset = " << name_str << "_reset,\n"
           << "    .read = " << name_str << "_read,\n"
           << "    .write = " << name_str << "_write,\n"
           << "    .destroy = " << name_str << "_destroy,\n"
           << "    .set_host_services = " << name_str << "_set_services,\n"
           << "    .advance_cycles = " << name_str << "_advance_cycles,\n"
           << "};\n\n"
           << "const SimRvExternalDevice* simrv_device_plugin_init(void) {\n"
           << "    return &kDevice;\n"
           << "}\n";
        return ss.str();
    };

    if (output == "-") {
        std::cout << generate_c_source();
        return true;
    }

    std::string out_path = output.empty() ? (name_str + "_plugin") : std::string(output);

    if (out_path.ends_with(".c")) {
        std::ofstream ofs(out_path);
        if (!ofs.is_open()) {
            simrv::log::error("Failed to open output file: {}", out_path);
            return false;
        }
        ofs << generate_c_source();
        std::println("Generated device plugin C source at {}", out_path);
        return true;
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(out_path, ec);
    if (ec) {
        simrv::log::error("Failed to create directory {}: {}", out_path, ec.message());
        return false;
    }

    const auto c_path = fs::path(out_path) / (name_str + "_plugin.c");
    const auto cmake_path = fs::path(out_path) / "CMakeLists.txt";
    const auto readme_path = fs::path(out_path) / "README.md";
    const auto cfg_path = fs::path(out_path) / (name_str + ".cfg");

    std::ofstream c_file(c_path);
    c_file << generate_c_source();

    std::ofstream cfg_file(cfg_path);
    cfg_file << "# Platform configuration for " << name_str << " device plugin\n\n"
             << "[soc]\n"
             << "name = \"" << name_str << "_soc\"\n"
             << "device_policy = \"allow-unlisted\"\n\n"
             << "[memory]\n"
             << "dram_base = 0x80000000\n"
             << "dram_size = 0x10000000\n\n"
             << "[plugins]\n"
             << name_str << " = \"./build/libsimrv-" << name_str << "-plugin.so\"\n\n"
             << "[device.custom-mmio." << name_str << "]\n"
             << "name = \"" << name_str << "\"\n"
             << "compatible = \"custom," << name_str << "\"\n"
             << "base = 0x10050000\n"
             << "size = 0x1000\n"
             << "irq = 16\n";

    std::ofstream cmake_file(cmake_path);
    cmake_file << "cmake_minimum_required(VERSION 3.20)\n"
               << "project(simrv-" << name_str << "-plugin LANGUAGES C)\n\n"
               << "# Find SimRV development package (provided by simrv-devel) if installed\n"
               << "find_package(SimRV 3.0 CONFIG QUIET COMPONENTS Development)\n\n"
               << "add_library(simrv-" << name_str << "-plugin SHARED " << name_str
               << "_plugin.c)\n\n"
               << "if(TARGET SimRV::simrv-devel)\n"
               << "    target_link_libraries(simrv-" << name_str
               << "-plugin PRIVATE SimRV::simrv-devel)\n"
               << "elseif(TARGET SimRV::devel)\n"
               << "    target_link_libraries(simrv-" << name_str
               << "-plugin PRIVATE SimRV::devel)\n"
               << "else()\n"
               << "    find_path(SIMRV_INCLUDE_DIR \"simrv/device/ExternalDevice.h\"\n"
               << "        HINTS ${SIMRV_DIR}/include $ENV{SIMRV_DIR}/include "
                  "${CMAKE_CURRENT_SOURCE_DIR}/../../../include\n"
               << "        PATH_SUFFIXES include\n"
               << "    )\n"
               << "    if(SIMRV_INCLUDE_DIR)\n"
               << "        target_include_directories(simrv-" << name_str
               << "-plugin PRIVATE ${SIMRV_INCLUDE_DIR})\n"
               << "    else()\n"
               << "        message(STATUS\n"
               << "            \"SimRV headers not found; using standard/local search paths.\")\n"
               << "    endif()\n"
               << "endif()\n\n"
               << "set_target_properties(simrv-" << name_str << "-plugin PROPERTIES\n"
               << "    PREFIX \"lib\"\n"
               << "    OUTPUT_NAME \"simrv-" << name_str << "-plugin\"\n"
               << ")\n";

    std::ofstream readme_file(readme_path);
    readme_file << "# " << name_str << " Device Plugin for SimRV\n\n"
                << "This is a dynamically loadable MMIO device plugin for SimRV conforming to "
                   "C ABI v2.\n\n"
                << "## Build Requirements\n"
                << "Header files are required at build time only:\n"
                << "- If `simrv-devel` is installed, CMake automatically links "
                   "`SimRV::simrv-devel`.\n"
                << "- Otherwise, specify `-DSIMRV_DIR=<path-to-simrv>`.\n\n"
                << "## Runtime Requirements\n"
                << "**None!** Developed plugins have zero runtime dependency on `simrv-devel`.\n"
                << "The compiled `.so` can be loaded by any standard `simrv` installation.\n\n"
                << "## Building with CMake\n"
                << "```bash\n"
                << "mkdir build && cd build\n"
                << "cmake ..\n"
                << "cmake --build .\n"
                << "```\n\n"
                << "## Running with SimRV\n\n"
                << "Option 1: Using the generated platform configuration:\n"
                << "```bash\n"
                << "simrv --cli --config " << name_str << ".cfg guest.elf\n"
                << "```\n\n"
                << "Option 2: Direct CLI plugin flag:\n"
                << "```bash\n"
                << "simrv --cli --plugin ./build/libsimrv-" << name_str << "-plugin.so guest.elf\n"
                << "```\n";

    std::println("Generated device plugin project template in {}", out_path);
    return true;
}

auto scaffold_cfu_plugin(std::string_view name, std::string_view output) -> bool {
    if (name.empty()) {
        simrv::log::error("No CFU plugin name specified for scaffolding.");
        return false;
    }

    const std::string name_str(name);

    auto generate_c_source = [&name_str]() -> std::string {
        std::ostringstream ss;
        ss << "/**\n"
           << " * @file " << name_str << "_plugin.c\n"
           << " * @brief Dynamic Custom Function Unit (CFU) plugin for " << name_str << ".\n"
           << " */\n"
           << "#include <stdint.h>\n"
           << "#include <stdlib.h>\n"
           << "#include \"simrv/execute/ExternalCfu.h\"\n\n"
           << "static uint32_t " << name_str
           << "_execute(uint32_t funct7, uint32_t funct3, uint32_t src1, uint32_t src2,\n"
           << "                             uint32_t* latency_cycles) {\n"
           << "    if (latency_cycles != NULL) {\n"
           << "        *latency_cycles = (funct3 == 1) ? 2 : 1;\n"
           << "    }\n"
           << "    switch (funct3) {\n"
           << "        case 0: return src1 + src2 + funct7;\n"
           << "        case 1: return src1 ^ src2;\n"
           << "        default: return src1 | src2;\n"
           << "    }\n"
           << "}\n\n"
           << "static uint64_t " << name_str
           << "_execute64(uint32_t funct7, uint32_t funct3, uint64_t src1, uint64_t src2,\n"
           << "                               uint32_t* latency_cycles) {\n"
           << "    if (latency_cycles != NULL) {\n"
           << "        *latency_cycles = (funct3 == 1) ? 2 : 1;\n"
           << "    }\n"
           << "    switch (funct3) {\n"
           << "        case 0: return src1 + src2 + (uint64_t)funct7;\n"
           << "        case 1: return src1 ^ src2;\n"
           << "        default: return src1 | src2;\n"
           << "    }\n"
           << "}\n\n"
           << "static void " << name_str << "_reset(void) {}\n\n"
           << "static uint32_t " << name_str
           << "_query_latency(uint32_t funct7, uint32_t funct3) {\n"
           << "    (void)funct7;\n"
           << "    return (funct3 == 1) ? 2 : 1;\n"
           << "}\n\n"
           << "static const struct SimRvCfuPlugin kPlugin = {\n"
           << "    .api_version = SIMRV_CFU_PLUGIN_API_VERSION,\n"
           << "    .execute = " << name_str << "_execute,\n"
           << "    .reset = " << name_str << "_reset,\n"
           << "    .execute64 = " << name_str << "_execute64,\n"
           << "    .query_latency = " << name_str << "_query_latency,\n"
           << "    .name = \"" << name_str << "\",\n"
           << "};\n\n"
           << "const struct SimRvCfuPlugin* simrv_cfu_init(void) {\n"
           << "    return &kPlugin;\n"
           << "}\n";
        return ss.str();
    };

    if (output == "-") {
        std::cout << generate_c_source();
        return true;
    }

    std::string out_path = output.empty() ? (name_str + "_cfu") : std::string(output);

    if (out_path.ends_with(".c")) {
        std::ofstream ofs(out_path);
        if (!ofs.is_open()) {
            simrv::log::error("Failed to open output file: {}", out_path);
            return false;
        }
        ofs << generate_c_source();
        std::println("Generated CFU plugin C source at {}", out_path);
        return true;
    }

    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(out_path, ec);
    if (ec) {
        simrv::log::error("Failed to create directory {}: {}", out_path, ec.message());
        return false;
    }

    const auto c_path = fs::path(out_path) / (name_str + "_plugin.c");
    const auto cmake_path = fs::path(out_path) / "CMakeLists.txt";
    const auto readme_path = fs::path(out_path) / "README.md";
    const auto cfg_path = fs::path(out_path) / (name_str + ".cfg");

    std::ofstream c_file(c_path);
    c_file << generate_c_source();

    std::ofstream cfg_file(cfg_path);
    cfg_file << "# CPU and platform configuration for " << name_str << " CFU plugin\n\n"
             << "[cpu]\n"
             << "preset = \"balanced\"\n\n"
             << "[cfu]\n"
             << "enabled = true\n"
             << "opcode = \"custom-0\"\n"
             << "plugin = \"./build/libsimrv-" << name_str << "-cfu.so\"\n"
             << "default_latency = 1\n";

    std::ofstream cmake_file(cmake_path);
    cmake_file << "cmake_minimum_required(VERSION 3.20)\n"
               << "project(simrv-" << name_str << "-cfu LANGUAGES C)\n\n"
               << "# Find SimRV development package (provided by simrv-devel) if installed\n"
               << "find_package(SimRV 3.0 CONFIG QUIET COMPONENTS Development)\n\n"
               << "add_library(simrv-" << name_str << "-cfu SHARED " << name_str << "_plugin.c)\n\n"
               << "if(TARGET SimRV::simrv-devel)\n"
               << "    target_link_libraries(simrv-" << name_str
               << "-cfu PRIVATE SimRV::simrv-devel)\n"
               << "elseif(TARGET SimRV::devel)\n"
               << "    target_link_libraries(simrv-" << name_str << "-cfu PRIVATE SimRV::devel)\n"
               << "else()\n"
               << "    find_path(SIMRV_INCLUDE_DIR \"simrv/execute/ExternalCfu.h\"\n"
               << "        HINTS ${SIMRV_DIR}/include $ENV{SIMRV_DIR}/include "
                  "${CMAKE_CURRENT_SOURCE_DIR}/../../../include\n"
               << "        PATH_SUFFIXES include\n"
               << "    )\n"
               << "    if(SIMRV_INCLUDE_DIR)\n"
               << "        target_include_directories(simrv-" << name_str
               << "-cfu PRIVATE ${SIMRV_INCLUDE_DIR})\n"
               << "    else()\n"
               << "        message(STATUS\n"
               << "            \"SimRV headers not found; proceeding with standard C build.\")\n"
               << "    endif()\n"
               << "endif()\n\n"
               << "set_target_properties(simrv-" << name_str << "-cfu PROPERTIES\n"
               << "    PREFIX \"lib\"\n"
               << "    OUTPUT_NAME \"simrv-" << name_str << "-cfu\"\n"
               << ")\n";

    std::ofstream readme_file(readme_path);
    readme_file << "# " << name_str << " Custom Function Unit (CFU) Plugin for SimRV\n\n"
                << "This is a dynamic Custom Function Unit (CFU) accelerator plugin for SimRV.\n"
                << "It supports both SimRV CFU ABI v2 and native CFU-Proving-Ground / "
                   "CFU-Playground conventions (`cfu_op` and `cfu_reset`).\n\n"
                << "## Build Requirements\n"
                << "Header files are required at build time only:\n"
                << "- If `simrv-devel` is installed, CMake automatically links "
                   "`SimRV::simrv-devel`.\n"
                << "- Otherwise, specify `-DSIMRV_DIR=<path-to-simrv>`.\n"
                << "- For raw CFU-Proving-Ground mode (`cfu_op`/`cfu_reset`), only standard "
                   "`<stdint.h>` is required.\n\n"
                << "## Runtime Requirements\n"
                << "**None!** Developed plugins have zero runtime dependency on `simrv-devel`.\n"
                << "The compiled `.so` can be loaded by any standard `simrv` installation.\n\n"
                << "## Building\n"
                << "```bash\n"
                << "mkdir build && cd build\n"
                << "cmake ..\n"
                << "cmake --build .\n"
                << "```\n\n"
                << "## Running with SimRV\n\n"
                << "Option 1: Using the generated platform configuration:\n"
                << "```bash\n"
                << "simrv --cli --config " << name_str << ".cfg guest.elf\n"
                << "```\n\n"
                << "Option 2: Direct CLI CFU flag:\n"
                << "```bash\n"
                << "simrv --cli --cfu ./build/libsimrv-" << name_str << "-cfu.so guest.elf\n"
                << "```\n";

    std::println("Generated CFU plugin project template in {}", out_path);
    return true;
}

auto scaffold_cpu_model(std::string_view preset_name, std::string_view output) -> bool {
    std::string target = preset_name.empty() ? "balanced" : std::string(preset_name);
    simrv::pipeline::CpuModelConfig cfg{};

    if (auto resolved = simrv::core::resolve_cpu_model_path(target)) {
        (void)simrv::core::load_cpu_config(*resolved, cfg);
    } else if (auto p = simrv::pipeline::parse_cpu_model_preset(target)) {
        cfg = simrv::pipeline::make_cpu_model_preset(*p);
    } else {
        cfg = simrv::pipeline::make_cpu_model_preset(simrv::pipeline::CpuModelPreset::Balanced);
    }

    if (!output.empty() && output != "-") {
        const std::string out_path(output);
        if (!simrv::core::save_cpu_config(out_path, cfg, target)) {
            simrv::log::error("Failed to write CPU model to {}", out_path);
            return false;
        }
        std::println("Wrote CPU model configuration to {}", out_path);
    } else {
        simrv::core::serialize_cpu_config(cfg, std::cout, target);
    }
    return true;
}

}  // namespace simrv::util
