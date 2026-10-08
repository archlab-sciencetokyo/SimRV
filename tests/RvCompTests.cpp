/**
 * @file RvCompTests.cpp
 * @brief Regression tests for RVComp cycle-accurate CPU model configuration and microarchitecture.
 */

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string_view>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/CpuConfigParser.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/MachineConfig.hpp"
#include "simrv/core/SoCDeviceRegistry.hpp"
#include "simrv/device/DummyMmio.hpp"
#include "simrv/device/PlatformDeviceRegistry.hpp"
#include "simrv/device/RingBufferEthernetMmio.hpp"
#include "simrv/device/ResetControlMmio.hpp"
#include "simrv/isa/Base.hpp"
#include "simrv/isa/Common.hpp"
#include "simrv/memory/MmioDevice.hpp"
#include "simrv/pipeline/CpuModel.hpp"
#include "simrv/util/SoCManifest.hpp"
#include "simrv/xlen/Types.hpp"

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

namespace {
class TestCustomMmio final : public simrv::memory::MmioDevice {
   public:
    explicit TestCustomMmio(const simrv::core::SoCDeviceConfig& config)
        : name_(config.name), base_(config.base), size_(config.size) {}
    [[nodiscard]] auto name() const -> const char* override { return name_.c_str(); }
    [[nodiscard]] auto base_address() const -> Address override { return base_; }
    [[nodiscard]] auto size() const -> Address override { return size_; }
    [[nodiscard]] auto read32(Address) -> uint32_t override { return 0x51a7; }

   private:
    std::string name_;
    Address base_;
    Address size_;
};
}  // namespace

void test_timing_front_cache() {
    simrv::cache::TimingCache cache;
    cache.configure(1024, 1, 16);
    TEST_CHECK(!cache.access(0x80000000).hit);
    TEST_CHECK(cache.access(0x80000004).hit);
    TEST_CHECK(!cache.access(0x80000010).hit);
    cache.flush();
    TEST_CHECK(!cache.access(0x80000004).hit);

    const std::array levels{
        simrv::cache::TimingLevelConfig{1024, 1, 16, 1, 27},
        simrv::cache::TimingLevelConfig{16384, 1, 32, 4, 12},
    };
    simrv::cache::TimingCacheHierarchy hierarchy;
    const std::array<uint32_t, 2> startup_refills{1, 41};
    hierarchy.configure(levels, 1, startup_refills);
    TEST_CHECK(hierarchy.access(0x80000000, true).latency == 1);
    TEST_CHECK(hierarchy.access(0x80000004, false).hit_level == 0);
    TEST_CHECK(hierarchy.access(0x80000020, true).latency == 41);
    TEST_CHECK(hierarchy.access(0x80000030, false).hit_level == 1);
}

void test_rvcomp_profile_validation() {
    std::cout << "[Test] RVComp config from configs/models/rvcomp.cfg...\n";
    const auto path = simrv::core::resolve_cpu_model_path("rvcomp");
    TEST_CHECK(path.has_value());

    simrv::pipeline::CpuModelConfig profile{};
    TEST_CHECK(simrv::core::parse_cpu_config(*path, profile));

    TEST_CHECK(profile.name == "rvcomp");
    TEST_CHECK(profile.supported_xlen == 32);
    TEST_CHECK(profile.isa_preset == IsaPreset::IMA);
    TEST_CHECK(profile.pipeline.pipeline_type == PipelineType::FiveStage);
    TEST_CHECK(profile.pipeline.enable_forwarding == true);
    TEST_CHECK(profile.pipeline.mul_latency == 2);
    TEST_CHECK(profile.pipeline.div_latency == 34);
    TEST_CHECK(profile.pipeline.branch_mispredict_penalty == 4);
    TEST_CHECK(profile.pipeline.cycle_counter_start_delay == 0);
    TEST_CHECK(profile.pipeline.host_interface_latency == 21);
    TEST_CHECK(profile.pipeline.host_interface_phase_period == 2);

    TEST_CHECK(profile.pipeline.branch_predictor.type == BranchPredictorType::Bimodal);
    TEST_CHECK(profile.pipeline.branch_predictor.btb_entries == 512);
    TEST_CHECK(profile.pipeline.branch_predictor.bht_entries == 8192);
    TEST_CHECK(profile.pipeline.branch_predictor.enable_ras == false);
    TEST_CHECK(profile.pipeline.branch_predictor.pc_shift == 2);
    TEST_CHECK(profile.pipeline.branch_predictor.untagged_btb == true);
    TEST_CHECK(profile.pipeline.branch_predictor.predict_non_control == true);
    TEST_CHECK(profile.pipeline.branch_predictor.registered_btb_read == false);
    TEST_CHECK(profile.pipeline.branch_predictor.bht_initial_state == 1);

    TEST_CHECK(profile.instruction_cache.capacity_bytes == 16384);
    TEST_CHECK(profile.instruction_cache.associativity == 1);
    TEST_CHECK(profile.instruction_cache.line_bytes == 32);
    TEST_CHECK(profile.instruction_cache.miss_latency == 1);
    TEST_CHECK(profile.instruction_front_cache.capacity_bytes == 1024);
    TEST_CHECK(profile.instruction_front_cache.line_bytes == 16);
    TEST_CHECK(profile.instruction_front_cache.refill_latency == 27);
    TEST_CHECK(profile.instruction_front_cache.backing_refill_latency == 27);
    TEST_CHECK(profile.instruction_front_cache.startup_refill_latencies ==
               std::vector<LatencyCycles>{40});
    TEST_CHECK(profile.instruction_front_cache.freeze_pipeline_on_refill);
    TEST_CHECK(profile.interconnect.request_latency == 1);
    TEST_CHECK(profile.interconnect.response_latency == 1);
    TEST_CHECK(profile.data_cache.miss_latency == 1);
    TEST_CHECK(profile.interconnect.data_request_latency == 30);
    TEST_CHECK(profile.interconnect.data_response_latency == 29);
    TEST_CHECK(profile.interconnect.startup_data_response_latency == 27);
    TEST_CHECK(profile.data_cache.capacity_bytes == 16384);
    TEST_CHECK(profile.data_cache.associativity == 1);
    TEST_CHECK(profile.data_cache.line_bytes == 32);
    TEST_CHECK(profile.data_cache.hit_latency == 4);

    simrv::pipeline::CpuModelConfig loaded{};
    TEST_CHECK(simrv::core::load_cpu_config(*path, loaded));
    TEST_CHECK(profile.validate().has_value());
    const CSRValue ima_bits = simrv::isa::isa_preset_bits(IsaPreset::IMA);
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
        TEST_CHECK(cpu.state().misa == (0x40141101U | (1U << 23)));
    } else {
        TEST_CHECK(cpu.state().misa == ((1ull << 62) | 0x00141101ULL | (1ull << 23)));
    }
}

void test_soc_metadata_parser() {
    std::cout << "[Test] SoC metadata parser...\n";
    const auto path = simrv::core::resolve_cpu_model_path("rvcomp");
    TEST_CHECK(path.has_value());

    auto soc = simrv::core::SoCConfig::rvcomp();
    TEST_CHECK(simrv::core::parse_soc_config(*path, soc));
    TEST_CHECK(soc.name == "rvcomp");
    TEST_CHECK(soc.disable_unlisted_devices);
    const auto* uart = soc.find(simrv::core::SoCDeviceKind::Uart);
    TEST_CHECK(uart != nullptr && uart->base == 0x10000000 && uart->size == 0x10 && uart->irq == 1);
    const auto* clint = soc.find(simrv::core::SoCDeviceKind::Clint);
    TEST_CHECK(clint != nullptr && clint->base == 0x02000000 && clint->size == 0x000c0000);
    const auto* plic = soc.find(simrv::core::SoCDeviceKind::Plic);
    TEST_CHECK(plic != nullptr && plic->base == 0x0c000000 && plic->size == 0x01000000);
    const auto* reset = soc.find(simrv::core::SoCDeviceKind::ResetControl);
    TEST_CHECK(reset != nullptr && reset->base == 0x10000100 && reset->size == 4);
    const auto* ethernet = soc.find(simrv::core::SoCDeviceKind::RingBufferEthernet);
    TEST_CHECK(ethernet != nullptr && ethernet->base == 0x14000000 && ethernet->irq == 2);
    TEST_CHECK(soc.find(simrv::core::SoCDeviceKind::DmaController) == nullptr);
    const auto resolved = simrv::core::SoCDeviceRegistry::resolve(soc);
    TEST_CHECK(resolved.size() == 5);
    TEST_CHECK(resolved[0].kind == simrv::core::SoCDeviceKind::Uart);
    TEST_CHECK(resolved[1].kind == simrv::core::SoCDeviceKind::Plic);
    TEST_CHECK(resolved[2].kind == simrv::core::SoCDeviceKind::Clint);
    TEST_CHECK(resolved[3].kind == simrv::core::SoCDeviceKind::ResetControl);
    TEST_CHECK(resolved[4].kind == simrv::core::SoCDeviceKind::RingBufferEthernet);

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
    TEST_CHECK(parsed.devices.size() == 11);
    const auto watchdog = std::ranges::find_if(parsed.devices, [](const auto& device) {
        return device.kind == simrv::core::SoCDeviceKind::DummyMmio &&
               device.name == "watchdog";
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
    auto soc = simrv::core::SoCConfig::rvcomp();
    const auto path = simrv::core::resolve_cpu_model_path("rvcomp");
    TEST_CHECK(path.has_value());
    TEST_CHECK(simrv::core::parse_soc_config(*path, soc));

    std::ostringstream manifest;
    TEST_CHECK(simrv::util::serialize_soc_manifest(soc, manifest));
    const auto text = manifest.str();
    TEST_CHECK(text.find("\"name\": \"rvcomp\"") != std::string::npos);
    TEST_CHECK(text.find("\"manifest_version\": \"3.0\"") != std::string::npos);
    TEST_CHECK(text.find("\"generator\": \"simrv ") != std::string::npos);
    TEST_CHECK(text.find("\"device_policy\": \"explicit\"") != std::string::npos);
    TEST_CHECK(text.find("\"kind\": \"uart\"") != std::string::npos);
    TEST_CHECK(text.find("\"kind\": \"plic\"") != std::string::npos);
    TEST_CHECK(text.find("\"kind\": \"clint\"") != std::string::npos);
    TEST_CHECK(text.find("\"kind\": \"reset-control\"") != std::string::npos);
    TEST_CHECK(text.find("\"kind\": \"ring-buffer-ethernet\"") != std::string::npos);
    TEST_CHECK(text.find("\"name\": \"rxbuf\"") != std::string::npos);
    TEST_CHECK(text.find("\"kind\": \"dma\"") == std::string::npos);

    auto custom_soc = simrv::core::SoCConfig::virt_mmio();
    custom_soc.disable_unlisted_devices = true;
    custom_soc.devices = {{simrv::core::SoCDeviceKind::CustomMmio, "custom-timer",
                           0x20003000, 0x1000, 0, true, {}, "acme,timer"}};
    custom_soc.devices.push_back(
        simrv::core::soc_device_default(simrv::core::SoCDeviceKind::ResetControl));
    std::ostringstream custom_manifest;
    TEST_CHECK(simrv::util::serialize_soc_manifest(custom_soc, custom_manifest));
    TEST_CHECK(custom_manifest.str().find("\"kind\": \"custom-mmio\"") != std::string::npos);
    TEST_CHECK(custom_manifest.str().find("\"compatible\": \"acme,timer\"") !=
               std::string::npos);

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
    auto* runtime_timer = custom_machine.memory().system_bus().router().find_by_name(
        "custom-timer");
    TEST_CHECK(runtime_timer != nullptr && runtime_timer->base_address() == 0x20003000 &&
               runtime_timer->size() == 0x1000);
    auto* runtime_mmio = dynamic_cast<simrv::memory::MmioDevice*>(runtime_timer);
    TEST_CHECK(runtime_mmio != nullptr && runtime_mmio->read32(0) == 0x51a7);
    auto* runtime_reset = custom_machine.memory().system_bus().router().find_by_name("reset");
    TEST_CHECK(runtime_reset != nullptr && runtime_reset->base_address() == 0x10000100);

    simrv::core::Machine isolated_machine(machine_config);
    TEST_CHECK(isolated_machine.initialize().has_value());
    auto* isolated_timer = isolated_machine.memory().system_bus().router().find_by_name(
        "custom-timer");
    auto* isolated_mmio = dynamic_cast<simrv::memory::MmioDevice*>(isolated_timer);
    TEST_CHECK(isolated_mmio != nullptr && isolated_mmio->read32(0) == 0);
    std::error_code ignored;
    std::filesystem::remove(image_path, ignored);
}

void test_rvcomp_reset_and_ethernet_extensions() {
    simrv::core::Machine machine(simrv::core::MachineConfig{
        .soc = simrv::core::SoCConfig::rvcomp(),
    });
    simrv::device::ResetControlMmio reset(machine, 0x10000100);
    reset.write32(0, 0xdeadbeef);
    TEST_CHECK(machine.reboot_requested);

    simrv::device::RingBufferEthernetMmio ethernet(
        machine, "ethernet", 0x14000000, 0x4000, 2, 0x18000000, 0x4000, 0x1c000000,
        0x2000, {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff},
        simrv::device::NetworkBackend::Mode::User);
    auto* tx = static_cast<simrv::memory::MmioDevice*>(ethernet.tx_node());
    auto* csr = static_cast<simrv::memory::MmioDevice*>(ethernet.csr_node());
    auto* rx = static_cast<simrv::memory::MmioDevice*>(ethernet.rx_node());
    tx->write32(0, 60);
    for (uint32_t word = 0; word < 15; ++word) {
        tx->write32(4 + word * 4, 0x03020100U + word * 0x04040404U);
    }
    csr->write32(6 * 4, 64);
    TEST_CHECK(ethernet.backend().tx_packet_count() == 1);
    ethernet.poll_backend();
    TEST_CHECK(csr->read32(4) == 60);
    TEST_CHECK(rx->read32(0) == 0x03020100U);
}

void test_dummy_mmio_behavior() {
    simrv::device::DummyMmio device("watchdog", 0x30000000, 0x1000, 0x12345678);
    TEST_CHECK(std::string_view(device.name()) == "watchdog");
    TEST_CHECK(device.base_address() == 0x30000000 && device.size() == 0x1000);
    TEST_CHECK(device.read32(0) == 0x12345678);
    device.write32(0, 0xffffffff);
    TEST_CHECK(device.read32(0) == 0x12345678);
}

int main() {
    test_timing_front_cache();
    std::cout << "=== Running RVComp Profile Tests ===" << std::endl;
    test_rvcomp_profile_validation();
    test_rvcomp_machine_application();
    test_soc_metadata_parser();
    test_soc_manifest_export();
    test_rvcomp_reset_and_ethernet_extensions();
    test_dummy_mmio_behavior();
    std::cout << "All RVComp profile tests passed successfully." << std::endl;
    return 0;
}
