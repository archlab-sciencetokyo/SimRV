/**
 * @file MicroarchitectureTests.cpp
 * @brief Regression tests for cycle-accurate CPU model configuration, timing front cache, and
 * microarchitecture.
 */

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>
#include <type_traits>

#include "TestHelper.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/CpuConfigParser.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/MachineConfig.hpp"
#include "simrv/core/SoCDeviceRegistry.hpp"
#include "simrv/core/TelemetrySink.hpp"
#include "simrv/device/DummyMmio.hpp"
#include "simrv/device/ExternalDevicePlugin.hpp"
#include "simrv/device/PlatformDeviceRegistry.hpp"
#include "simrv/device/ResetControlMmio.hpp"
#include "simrv/device/RingBufferEthernetMmio.hpp"
#include "simrv/device/SerialDevice.hpp"
#include "simrv/isa/Common.hpp"
#include "simrv/isa/IsaTypes.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/MmioDevice.hpp"
#include "simrv/memory/MmioRouter.hpp"
#include "simrv/pipeline/CpuModel.hpp"
#include "simrv/util/SoCManifest.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

static_assert(SIMRV_EXTERNAL_DEVICE_API_VERSION == 2);
static_assert(SIMRV_EXTERNAL_DEVICE_API_VERSION_V1 == 1);
static_assert(std::is_standard_layout_v<SimRvExternalDevice>);
static_assert(std::is_standard_layout_v<SimRvHostServices>);
static_assert(std::is_trivially_copyable_v<SimRvExternalDevice>);
static_assert(offsetof(SimRvExternalDevice, api_version) == 0);
static_assert(sizeof(void*) != 8 ||
              (sizeof(SimRvExternalDevice) == 96 && sizeof(SimRvHostServices) == 64 &&
               offsetof(SimRvExternalDevice, default_base) == 24 &&
               offsetof(SimRvExternalDevice, init) == 40 &&
               offsetof(SimRvExternalDevice, set_host_services) == 80 &&
               offsetof(SimRvExternalDevice, advance_cycles) == 88));

using simrv::isa::IsaExtension;
using simrv::pipeline::BranchPredictorType;
using simrv::pipeline::CpuModelPreset;
using simrv::pipeline::PipelineType;

namespace {
template <typename Address>
concept InstructionTimingAddress =
    requires(simrv::cache::TimingCache& cache, simrv::cache::TimingCacheHierarchy& hierarchy,
             Address address) {
        cache.access(address);
        hierarchy.access(address, true);
    };

static_assert(InstructionTimingAddress<simrv::xlen::VirtAddr>);
static_assert(!InstructionTimingAddress<simrv::xlen::PhysAddr>);
static_assert(!InstructionTimingAddress<simrv::xlen::Address>);

class TestCustomMmio final : public simrv::memory::MmioDevice {
   public:
    explicit TestCustomMmio(const simrv::core::SoCDeviceConfig& config)
        : name_(config.name), base_(config.base.raw()), size_(config.size) {}
    [[nodiscard]] auto name() const -> const char* override { return name_.c_str(); }
    [[nodiscard]] auto base_address() const -> simrv::xlen::PhysAddr override {
        return simrv::xlen::PhysAddr{base_};
    }
    [[nodiscard]] auto size() const -> simrv::xlen::Address override { return size_; }
    [[nodiscard]] auto read32(simrv::memory::MmioOffset) -> uint32_t override { return 0x51a7; }

   private:
    std::string name_;
    simrv::xlen::Address base_;
    simrv::xlen::Address size_;
};
}  // namespace

void test_timing_front_cache() {
    simrv::cache::TimingCache cache;
    cache.configure(1024, 1, 16);
    TEST_CHECK(!cache.access(simrv::xlen::VirtAddr{0x80000000}).hit);
    TEST_CHECK(cache.access(simrv::xlen::VirtAddr{0x80000004}).hit);
    TEST_CHECK(!cache.access(simrv::xlen::VirtAddr{0x80000010}).hit);
    cache.flush();
    TEST_CHECK(!cache.access(simrv::xlen::VirtAddr{0x80000004}).hit);

    const std::array levels{
        simrv::cache::TimingLevelConfig{1024, 1, 16, 1, 27},
        simrv::cache::TimingLevelConfig{16384, 1, 32, 4, 12},
    };
    simrv::cache::TimingCacheHierarchy hierarchy;
    const std::array<uint32_t, 2> startup_refills{1, 41};
    hierarchy.configure(levels, 1, startup_refills);
    TEST_CHECK(hierarchy.access(simrv::xlen::VirtAddr{0x80000000}, true).latency == 1);
    TEST_CHECK(hierarchy.access(simrv::xlen::VirtAddr{0x80000004}, false).hit_level == 0);
    TEST_CHECK(hierarchy.access(simrv::xlen::VirtAddr{0x80000020}, true).latency == 41);
    TEST_CHECK(hierarchy.access(simrv::xlen::VirtAddr{0x80000030}, false).hit_level == 1);
}

void test_soc_metadata_parser() {
    std::cout << "[Test] generic SoC metadata parser...\n";
    std::istringstream custom(R"cfg(
[soc]
device_policy = "explicit"
platform = "mmio"
[device.virtio-net]
name = "net1"
base = 0x20000000
size = 8K
irq = 19
[device.dummy-mmio.watchdog]
base = 0x30000000
size = 4K
compatible = "acme,watchdog"
read_value = 0xffffffff
[device.dummy-mmio.control]
base = 0x30001000
size = 4K
compatible = "acme,control"
read_value = 7
[device.custom-mmio.timer]
base = 0x30002000
size = 4K
compatible = "acme,timer"
)cfg");
    simrv::core::SoCConfig parsed = simrv::core::SoCConfig::virt_mmio();
    TEST_CHECK(simrv::core::parse_soc_config_stream(custom, parsed));
    const auto* net = parsed.find(simrv::core::SoCDeviceKind::VirtioMmioNet);
    TEST_CHECK(net != nullptr && net->name == "net1" && net->base == 0x20000000 &&
               net->size == 8192 && net->irq == 19);
    TEST_CHECK(parsed.devices.size() == 12);
    const auto watchdog = std::ranges::find_if(parsed.devices, [](const auto& device) {
        return device.kind == simrv::core::SoCDeviceKind::DummyMmio && device.name == "watchdog";
    });
    const auto control = std::ranges::find_if(parsed.devices, [](const auto& device) {
        return device.kind == simrv::core::SoCDeviceKind::DummyMmio && device.name == "control";
    });
    TEST_CHECK(watchdog != parsed.devices.end() && watchdog->base == 0x30000000 &&
               watchdog->read_value == 0xffffffff && watchdog->compatible == "acme,watchdog");
    TEST_CHECK(control != parsed.devices.end() && control->base == 0x30001000 &&
               control->read_value == 7 && control->compatible == "acme,control");
    const auto timer = std::ranges::find_if(parsed.devices, [](const auto& item) {
        return item.kind == simrv::core::SoCDeviceKind::CustomMmio && item.name == "timer";
    });
    TEST_CHECK(timer != parsed.devices.end() && timer->base == 0x30002000 &&
               timer->compatible == "acme,timer");
    TEST_CHECK(parsed.validate().has_value());
}

void test_soc_manifest_export() {
    auto custom_soc = simrv::core::SoCConfig::virt_mmio();
    custom_soc.disable_unlisted_devices = true;
    custom_soc.devices = {{simrv::core::SoCDeviceKind::CustomMmio,
                           "custom-timer",
                           simrv::xlen::PhysAddr{0x20003000},
                           0x1000,
                           0,
                           true,
                           {},
                           "acme,timer"}};
    custom_soc.devices.front().name = "custom\x01timer";
    custom_soc.devices.push_back(
        simrv::core::soc_device_default(simrv::core::SoCDeviceKind::ResetControl));
    std::ostringstream custom_manifest;
    TEST_CHECK(simrv::util::serialize_soc_manifest(custom_soc, custom_manifest));
    // Keep control characters JSON-safe rather than emitting invalid raw bytes.
    TEST_CHECK(custom_manifest.str().find("\"kind\": \"custom-mmio\"") != std::string::npos);
    TEST_CHECK(custom_manifest.str().find("\"compatible\": \"acme,timer\"") != std::string::npos);
    TEST_CHECK(custom_manifest.str().find("custom\\u0001timer") != std::string::npos);
    custom_soc.devices.front().name = "custom-timer";

    simrv::core::MachineConfig machine_config{};
    machine_config.soc = custom_soc;
    const auto image_path = std::filesystem::temp_directory_path() / "simrv-custom-mmio-test.bin";
    {
        std::ofstream image(image_path, std::ios::binary);
        image.put('\0');
        image.put('\0');
        image.put('\0');
        image.put('\0');
    }
    machine_config.files.binary_path = image_path.string();
    simrv::core::Machine custom_machine(machine_config);
    TEST_CHECK(custom_machine.device_registry().register_mmio_compatible(
        "acme,timer", [](simrv::core::Machine&, const simrv::core::SoCDeviceConfig& config) {
            return std::make_unique<TestCustomMmio>(config);
        }));
    TEST_CHECK(custom_machine.initialize().has_value());
    auto* runtime_timer =
        custom_machine.memory().system_bus().router().find_by_name("custom-timer");
    TEST_CHECK(runtime_timer != nullptr && runtime_timer->base_address() == 0x20003000 &&
               runtime_timer->size() == 0x1000);
    auto* runtime_mmio = dynamic_cast<simrv::memory::MmioDevice*>(runtime_timer);
    TEST_CHECK(runtime_mmio != nullptr &&
               runtime_mmio->read32(simrv::memory::MmioOffset{0}) == 0x51a7);
    auto* runtime_reset = custom_machine.memory().system_bus().router().find_by_name("reset");
    TEST_CHECK(runtime_reset != nullptr && runtime_reset->base_address() == 0x10000100);

    simrv::core::Machine isolated_machine(machine_config);
    TEST_CHECK(isolated_machine.initialize().has_value());
    auto* isolated_timer =
        isolated_machine.memory().system_bus().router().find_by_name("custom-timer");
    auto* isolated_mmio = dynamic_cast<simrv::memory::MmioDevice*>(isolated_timer);
    TEST_CHECK(isolated_mmio != nullptr &&
               isolated_mmio->read32(simrv::memory::MmioOffset{0}) == 0);
    std::error_code ignored;
    std::filesystem::remove(image_path, ignored);
}

void test_dummy_mmio_behavior() {
    simrv::device::DummyMmio device("watchdog", simrv::xlen::PhysAddr{0x30000000}, 0x1000,
                                    0x12345678);
    TEST_CHECK(std::string_view(device.name()) == "watchdog");
    TEST_CHECK(device.base_address() == 0x30000000 && device.size() == 0x1000);
    TEST_CHECK(device.read32(simrv::memory::MmioOffset{0}) == 0x12345678);
    device.write32(simrv::memory::MmioOffset{0}, 0xffffffff);
    TEST_CHECK(device.read32(simrv::memory::MmioOffset{0}) == 0x12345678);
}

void test_vexriscv_profile_validation() {
    std::cout << "[Test] VexRiscv profile validation...\n";
    auto path = simrv::core::resolve_cpu_model_path("vexriscv");
    TEST_CHECK(path.has_value());

    simrv::pipeline::CpuModelConfig profile{};
    TEST_CHECK(simrv::core::load_cpu_config(*path, profile));
    TEST_CHECK(profile.validate().has_value());
    TEST_CHECK(profile.name == "vexriscv");
    TEST_CHECK(profile.supported_xlen == 32);
    TEST_CHECK(profile.isa.misa_bits == simrv::isa::parse_isa_spec("imac").value().misa_bits);
    TEST_CHECK(profile.pipeline.pipeline_type == simrv::pipeline::PipelineType::FiveStage);
    TEST_CHECK(profile.pipeline.enable_forwarding);
    TEST_CHECK(profile.pipeline.mul_latency == 2);
    TEST_CHECK(profile.pipeline.div_latency == 33);
    TEST_CHECK(profile.pipeline.branch_mispredict_penalty == 3);
    TEST_CHECK(profile.pipeline.branch_predictor.type ==
               simrv::pipeline::BranchPredictorType::Bimodal);
    TEST_CHECK(profile.pipeline.branch_predictor.btb_entries == 512);
    TEST_CHECK(profile.pipeline.branch_predictor.bht_entries == 1024);
    TEST_CHECK(profile.instruction_cache.capacity_bytes == 4096);
    TEST_CHECK(profile.instruction_cache.associativity == 1);
    TEST_CHECK(profile.data_cache.capacity_bytes == 4096);
    TEST_CHECK(profile.data_cache.associativity == 1);

    simrv::core::Machine machine;
    auto& cpu = machine.primary_hart();
    cpu.machine_ = &machine;
    cpu.reset();
    cpu.apply_cpu_model_config(profile);
    TEST_CHECK(cpu.cpu_model_config.name == "vexriscv");
    TEST_CHECK(cpu.state().regs.xlen == 32);
    TEST_CHECK(cpu.pipeline_sim.config.pipeline_type == simrv::pipeline::PipelineType::FiveStage);
}

void test_ibex_profile_validation() {
    std::cout << "[Test] Ibex profile validation...\n";
    auto path = simrv::core::resolve_cpu_model_path("ibex");
    TEST_CHECK(path.has_value());

    simrv::pipeline::CpuModelConfig profile{};
    TEST_CHECK(simrv::core::load_cpu_config(*path, profile));
    TEST_CHECK(profile.validate().has_value());
    TEST_CHECK(profile.name == "ibex");
    TEST_CHECK(profile.supported_xlen == 32);
    TEST_CHECK(profile.isa.misa_bits == simrv::isa::parse_isa_spec("imac").value().misa_bits);
    TEST_CHECK(profile.pipeline.pipeline_type == simrv::pipeline::PipelineType::ThreeStage);
    TEST_CHECK(!profile.pipeline.enable_forwarding);
    TEST_CHECK(profile.pipeline.mul_latency == 0);
    TEST_CHECK(profile.pipeline.div_latency == 35);
    TEST_CHECK(profile.pipeline.branch_mispredict_penalty == 2);
    TEST_CHECK(profile.pipeline.branch_predictor.type ==
               simrv::pipeline::BranchPredictorType::Static);
    TEST_CHECK(profile.instruction_cache.capacity_bytes == 4096);
    TEST_CHECK(profile.instruction_cache.associativity == 2);
    TEST_CHECK(profile.data_cache.capacity_bytes == 4096);
    TEST_CHECK(profile.data_cache.hit_latency == 1);

    simrv::core::Machine machine;
    auto& cpu = machine.primary_hart();
    cpu.machine_ = &machine;
    cpu.reset();
    cpu.apply_cpu_model_config(profile);
    TEST_CHECK(cpu.cpu_model_config.name == "ibex");
    TEST_CHECK(cpu.state().regs.xlen == 32);
    TEST_CHECK(cpu.pipeline_sim.config.pipeline_type == simrv::pipeline::PipelineType::ThreeStage);
}

int main() {
    test_timing_front_cache();
    test_vexriscv_profile_validation();
    test_ibex_profile_validation();
    test_soc_metadata_parser();
    test_soc_manifest_export();
    test_dummy_mmio_behavior();

    return 0;
}
