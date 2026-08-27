/**
 * @file MachineConfig.hpp
 * @brief Value configuration shared by machine, platform, and memory setup.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

#include "simrv/Define.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/pipeline/PipelineSim.hpp"

namespace simrv::core {

enum class PlatformProfile : uint8_t {
    Pcie = 0,
    Mmio = 1,
    Hybrid = 2,
};

struct MemoryGeometry {
    Address dram_base = memory::kDramBaseAddress;
    Address dram_size = memory::kDramSize;

    [[nodiscard]] constexpr auto contains(Address address, size_t size = 1) const -> bool {
        return memory::address_range_contains(dram_base, dram_size, address, size);
    }
};

struct ExecutionConfig {
    bool appmode = true;
    bool multithreaded = false;
    uint32_t num_harts = 1;
    uint32_t smp_quantum = 100;
    bool smp_multithreaded = false;
    simrv::pipeline::PipelineType pipeline_type = simrv::pipeline::PipelineType::FiveStage;
    Address start_pc = 0;
    Counter strace = 0;
    Counter fincnt = std::numeric_limits<Counter>::max();
    Counter trace_begin = std::numeric_limits<Counter>::max();
    Counter trace_end = std::numeric_limits<Counter>::max();
    Counter enabletimer = std::numeric_limits<Counter>::max();
    Counter memimg_cycle = std::numeric_limits<Counter>::max();
};

struct TuiConfig {
    bool enabled = false;
    bool high_contrast = false;
    bool class_mode = false;
    bool debug_diagnostics = false;
    double mouse_sensitivity = 1.0;
};

struct DebugConfig {
    bool gdb_enabled = false;
    uint16_t gdb_port = 1234;
    bool lockstep_enabled = false;
    std::string spike_bin = "spike";
    std::string spike_elf;
    bool debugmode = false;
    bool dlog_mode = false;
    bool traplog_mode = false;
    bool bp_trace = false;
    bool use_mix = false;
};

struct IsaConfig {
    Address isatest_tohost = 0x80001000;
    CSRValue misa_profile = isa::kMisaDefault;
    bool misa_override = false;
    unsigned int misa_xlen = 0;
    unsigned int vlen = 0;
};

struct FilesConfig {
    std::string binary_path;
    std::string disk_path;
    std::string memimg_path;
    std::string dvtree_path;
    std::string traplog_path;
    std::string cpuconfig_path;
};

struct NetworkConfig {
    std::string mode = "user";
};

struct MachineConfig {
    MemoryGeometry memory{};
    ExecutionConfig execution{};
    TuiConfig tui{};
    DebugConfig debug{};
    IsaConfig isa{};
    FilesConfig files{};
    NetworkConfig network{};
    unsigned core_count = SIMRV_CORE_COUNT;
    unsigned disk_size_mb = SIMRV_DISK_SIZE_MB;
    PlatformProfile platform_profile = PlatformProfile::Pcie;
};

}  // namespace simrv::core
