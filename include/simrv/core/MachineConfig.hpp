/**
 * @file MachineConfig.hpp
 * @brief Value configuration shared by machine, platform, and memory setup.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <string>
#include <utility>

#include "simrv/Define.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/pipeline/CpuModel.hpp"
#include "simrv/pipeline/PipelineSim.hpp"

namespace simrv::core {

enum class PlatformProfile : uint8_t {
    Pcie = 0,
    Mmio = 1,
};

struct MemoryGeometry {
    Address dram_base = memory::kDramBaseAddress;
    Address dram_size = memory::kDramSize;

    [[nodiscard]] constexpr auto contains(Address address, size_t size = 1) const -> bool {
        return memory::address_range_contains(dram_base, dram_size, address, size);
    }
    [[nodiscard]] constexpr auto contains(PhysAddr address, size_t size = 1) const -> bool {
        return memory::address_range_contains(dram_base, dram_size, address.raw(), size);
    }
};

struct ExecutionConfig {
    // ACLINT and AIA allocate one interrupt file per simulated hart.
    static constexpr uint32_t kMaxHarts = 16;

    bool appmode = true;
    bool ui_worker_threaded = false;
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
    /// Optional external classroom mission path. Missions are local guidance only.
    std::string mission = {};
    double mouse_sensitivity = 1.0;
    std::string inspection_output = {};
};

struct DebugConfig {
    bool gdb_enabled = false;
    uint16_t gdb_port = 1234;
    bool lockstep_enabled = false;
    std::string spike_bin = "spike";
    std::string spike_elf;
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
    bool disk_enabled = false;
    std::string memimg_path;
    std::string dvtree_path;
    std::string traplog_path;
    std::string cpuconfig_path;
    std::string cfu_plugin_path;
    std::string dump_dmem_path;
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
    std::optional<simrv::pipeline::CpuModelProfile> cpu_model_profile{};
    unsigned disk_size_mb = SIMRV_DISK_SIZE_MB;
    PlatformProfile platform_profile = PlatformProfile::Pcie;

    [[nodiscard]] auto validate() const -> std::expected<void, std::string> {
        if (memory.dram_size == 0 || (memory.dram_size & (memory.dram_size - 1U)) != 0) {
            return std::unexpected("DRAM size must be a non-zero power of two");
        }
        if (execution.num_harts == 0 || execution.num_harts > ExecutionConfig::kMaxHarts) {
            return std::unexpected("hart count must be between 1 and 16");
        }
        if (execution.smp_quantum == 0) {
            return std::unexpected("SMP quantum must be non-zero");
        }
        if (isa.misa_xlen != 0 && isa.misa_xlen != 32 && isa.misa_xlen != 64) {
            return std::unexpected("MISA XLEN must be 32, 64, or unspecified");
        }
        if (isa.vlen != 0 && (isa.vlen < 32 || isa.vlen > 1024 || (isa.vlen % 32) != 0)) {
            return std::unexpected("VLEN must be a multiple of 32 between 32 and 1024");
        }
        if (files.disk_enabled && files.disk_path.empty()) {
            return std::unexpected("a disk path is required when disk support is enabled");
        }
        if (tui.enabled && debug.gdb_enabled) {
            return std::unexpected("GDB remote debugging and the TUI are mutually exclusive");
        }
        if (debug.gdb_enabled && debug.lockstep_enabled) {
            return std::unexpected(
                "GDB remote debugging and Spike lockstep are mutually exclusive");
        }
        return {};
    }

    template <typename Self>
    constexpr auto&& with_dram_base(this Self&& self, Address base) noexcept {
        self.memory.dram_base = base;
        return std::forward<Self>(self);
    }

    template <typename Self>
    constexpr auto&& with_dram_size(this Self&& self, Address size) noexcept {
        self.memory.dram_size = size;
        return std::forward<Self>(self);
    }

    template <typename Self>
    constexpr auto&& with_harts(this Self&& self, uint32_t count) noexcept {
        self.execution.num_harts = count;
        return std::forward<Self>(self);
    }

    template <typename Self>
    constexpr auto&& with_appmode(this Self&& self, bool mode) noexcept {
        self.execution.appmode = mode;
        return std::forward<Self>(self);
    }

    template <typename Self>
    constexpr auto&& with_smp_quantum(this Self&& self, uint32_t quantum) noexcept {
        self.execution.smp_quantum = quantum;
        return std::forward<Self>(self);
    }

    template <typename Self>
    constexpr auto&& with_pipeline(this Self&& self, simrv::pipeline::PipelineType type) noexcept {
        self.execution.pipeline_type = type;
        return std::forward<Self>(self);
    }

    template <typename Self>
    constexpr auto&& with_start_pc(this Self&& self, Address pc) noexcept {
        self.execution.start_pc = pc;
        return std::forward<Self>(self);
    }

    template <typename Self>
    constexpr auto&& with_tui(this Self&& self, bool enabled) noexcept {
        self.tui.enabled = enabled;
        return std::forward<Self>(self);
    }

    template <typename Self>
    constexpr auto&& with_gdb(this Self&& self, bool enabled, uint16_t port = 1234) noexcept {
        self.debug.gdb_enabled = enabled;
        self.debug.gdb_port = port;
        return std::forward<Self>(self);
    }

    template <typename Self>
    constexpr auto&& with_misa_xlen(this Self&& self, unsigned int xlen) noexcept {
        self.isa.misa_xlen = xlen;
        return std::forward<Self>(self);
    }

    template <typename Self>
    constexpr auto&& with_vlen(this Self&& self, unsigned int vlen) noexcept {
        self.isa.vlen = vlen;
        return std::forward<Self>(self);
    }

    template <typename Self>
    constexpr auto&& with_binary(this Self&& self, std::string path) {
        self.files.binary_path = std::move(path);
        return std::forward<Self>(self);
    }

    template <typename Self>
    constexpr auto&& with_disk(this Self&& self, std::string path) {
        self.files.disk_path = std::move(path);
        self.files.disk_enabled = !self.files.disk_path.empty();
        return std::forward<Self>(self);
    }

    template <typename Self>
    constexpr auto&& with_platform_profile(this Self&& self, PlatformProfile profile) noexcept {
        self.platform_profile = profile;
        return std::forward<Self>(self);
    }
};

}  // namespace simrv::core
