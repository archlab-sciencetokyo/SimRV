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

using simrv::isa::IsaPreset;
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
               std::vector<simrv::xlen::LatencyCycles>{40});
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
    const simrv::xlen::CsrValue ima_bits = simrv::isa::isa_preset_bits(IsaPreset::IMA);
    const simrv::xlen::CsrValue ima_misa = simrv::isa::misa_with_mxl(ima_bits, 32);
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
    TEST_CHECK(uart->compatible == "isct,rvcomp-uart");
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
    TEST_CHECK(text.find("\"smp\": {") != std::string::npos);
    TEST_CHECK(text.find("\"mode\": \"single-threaded\"") != std::string::npos);
    TEST_CHECK(text.find("\"l3\": {\"enabled\": true") != std::string::npos);
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

void test_rvcomp_reset_and_ethernet_extensions() {
    simrv::core::Machine machine(simrv::core::MachineConfig{
        .soc = simrv::core::SoCConfig::rvcomp(),
    });
    simrv::device::ResetControlMmio reset(machine, simrv::xlen::PhysAddr{0x10000100});
    reset.write32(simrv::memory::MmioOffset{0}, 0xdeadbeef);
    TEST_CHECK(machine.reboot_requested);

    simrv::device::RingBufferEthernetMmio ethernet(
        machine, "ethernet", simrv::xlen::PhysAddr{0x14000000}, 0x4000, 2,
        simrv::xlen::PhysAddr{0x18000000}, 0x4000, simrv::xlen::PhysAddr{0x1c000000}, 0x2000,
        {0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff}, simrv::device::virtio::NetBackend::Mode::User);
    auto* tx = static_cast<simrv::memory::MmioDevice*>(ethernet.tx_node());
    auto* csr = static_cast<simrv::memory::MmioDevice*>(ethernet.csr_node());
    auto* rx = static_cast<simrv::memory::MmioDevice*>(ethernet.rx_node());
    constexpr std::array<uint8_t, 6> mac{0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff};
    std::array<uint8_t, 42> arp{};
    std::fill_n(arp.begin(), 6, uint8_t{0xff});
    std::copy(mac.begin(), mac.end(), arp.begin() + 6);
    arp[12] = 0x08;
    arp[13] = 0x06;
    arp[14] = 0;
    arp[15] = 1;
    arp[16] = 0x08;
    arp[17] = 0;
    arp[18] = 6;
    arp[19] = 4;
    arp[21] = 1;
    std::copy(mac.begin(), mac.end(), arp.begin() + 22);
    arp[28] = 10;
    arp[30] = 2;
    arp[31] = 15;
    arp[38] = 10;
    arp[40] = 2;
    arp[41] = 2;
    tx->write32(simrv::memory::MmioOffset{0}, static_cast<uint32_t>(arp.size()));
    for (uint32_t word = 0; word < (arp.size() + 3) / 4; ++word) {
        uint32_t value = 0;
        for (uint32_t byte = 0; byte < 4; ++byte) {
            const auto index = word * 4 + byte;
            if (index < arp.size()) value |= static_cast<uint32_t>(arp[index]) << (byte * 8);
        }
        tx->write32(simrv::memory::MmioOffset{4 + word * 4}, value);
    }
    csr->write32(simrv::memory::MmioOffset{6 * 4},
                 static_cast<uint32_t>((4 + arp.size() + 3) & ~std::size_t{3}));
    TEST_CHECK(ethernet.backend().tx_packet_count() == 1);
    // Exercise a guest-style TX ring packet through libslirp and the ring-buffer RX adapter.
    TEST_CHECK(ethernet.backend().has_rx_packet());
    ethernet.poll_backend();
    TEST_CHECK(csr->read32(simrv::memory::MmioOffset{4}) >= arp.size());
    const uint32_t arp_reply = rx->read32(simrv::memory::MmioOffset{0});
    TEST_CHECK(arp_reply == 0xddccbbaaU);
    TEST_CHECK((rx->read32(simrv::memory::MmioOffset{12}) & 0xffffU) == 0x0608U);
    TEST_CHECK(rx->read32(simrv::memory::MmioOffset{20}) != 0);
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

#if defined(SIMRV_TEST_RVCOMP_UART)
void test_rvcomp_uart_plugin() {
    struct Capture final : simrv::core::IConsoleSink {
        std::string output;
        void handle_char_write(char ch) override { output.push_back(ch); }
    };
    simrv::core::MachineConfig config{};
    config.soc = simrv::core::SoCConfig::rvcomp();
    const auto path = simrv::core::resolve_cpu_model_path("rvcomp");
    TEST_CHECK(path.has_value() && simrv::core::parse_soc_config(*path, config.soc));
    config.tui.enabled = true;
    simrv::core::Machine machine(config);
    auto capture = std::make_shared<Capture>();
    machine.set_console_sink(capture);
    TEST_CHECK(machine.initialize().has_value());
    auto* uart = machine.uart_device();
    TEST_CHECK(uart != nullptr);
    simrv::memory::TlChannelA request{};
    simrv::memory::TlChannelD response{};
    request.size = 0;
    request.opcode = simrv::memory::TlOpcodeA::Get;
    request.address = simrv::xlen::PhysAddr{0x10000004};
    TEST_CHECK(uart->handle_request(request, response) && !response.denied && response.data == 0);
    request.opcode = simrv::memory::TlOpcodeA::PutFullData;
    TEST_CHECK(uart->handle_request(request, response) && response.denied);
    request.opcode = simrv::memory::TlOpcodeA::Get;
    request.address = simrv::xlen::PhysAddr{0x10000008};
    TEST_CHECK(uart->handle_request(request, response) && !response.denied && response.data == 1);
    uart->push_rx_byte('A');
    uart->service_interrupts();
    TEST_CHECK(uart->is_interrupt_pending());
    TEST_CHECK((machine.primary_hart().plic_mmio.plic_pending[0] & (1U << 1)) != 0);
    TEST_CHECK(uart->handle_request(request, response) && !response.denied && response.data == 0);
    request.address = simrv::xlen::PhysAddr{0x10000000};
    TEST_CHECK(uart->handle_request(request, response) && !response.denied && response.data == 'A');
    request.address = simrv::xlen::PhysAddr{0x10000008};
    TEST_CHECK(uart->handle_request(request, response) && !response.denied && response.data == 1);
    uart->service_interrupts();
    TEST_CHECK(!uart->is_interrupt_pending());
    TEST_CHECK((machine.primary_hart().plic_mmio.plic_pending[0] & (1U << 1)) == 0);

    uart->push_rx_byte(0);
    TEST_CHECK(uart->handle_request(request, response) && response.data == 0);
    request.address = simrv::xlen::PhysAddr{0x10000000};
    TEST_CHECK(uart->handle_request(request, response) && response.data == 0);
    request.opcode = simrv::memory::TlOpcodeA::PutFullData;
    request.data = 'Z';
    TEST_CHECK(uart->handle_request(request, response) && !response.denied);
    request.data = 0;
    TEST_CHECK(uart->handle_request(request, response) && !response.denied);
    TEST_CHECK(capture->output.size() == 2 && capture->output[0] == 'Z' && capture->output[1] == 0);

    // RVComp firmware first probes TX-full, then polls RX-empty before echoing input.
    constexpr std::array<simrv::xlen::Instruction, 7> guest = {
        0x100002b7, 0x0042c303, 0x0082c303, 0xfe031ee3, 0x0002c303, 0x00628023, 0xff1ff06f,
    };
    uart->reset();
    capture->output.clear();
    std::memcpy(machine.ram_data(), guest.data(), sizeof(guest));
    auto& cpu = machine.primary_hart();
    cpu.state().pc = simrv::xlen::VirtAddr{simrv::memory::kDramBaseAddress.raw()};
    for (int i = 0; i < 1000; ++i) cpu.run_cycle(machine);
    TEST_CHECK(capture->output.empty());
    uart->push_rx_byte('Q');
    for (int i = 0; i < 1000; ++i) cpu.run_cycle(machine);
    TEST_CHECK(capture->output == "Q");
}
#endif

void test_rvcomp_mmc_plugin_and_device() {
    std::cout << "[Test] RVComp MMC external plugin and block device...\n";
    simrv::core::Machine machine(simrv::core::MachineConfig{
        .soc = simrv::core::SoCConfig::rvcomp(),
    });

    // 1. Full disk image test (with 32 MiB prefix and ext4 magic at 32 MiB + 0x438)
    const auto full_image_path =
        std::filesystem::temp_directory_path() / "simrv-rvcomp-mmc-full.img";
    {
        std::ofstream img(full_image_path, std::ios::binary | std::ios::trunc);
        img.seekp(0x2000000ULL + 65536ULL - 1);
        img.put('\0');
        img.seekp(0x2000000ULL + 0x438ULL);
        const uint16_t magic = 0xef53;
        img.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        img.seekp(0x2000000ULL);
        const uint32_t payload = 0x11223344U;
        img.write(reinterpret_cast<const char*>(&payload), sizeof(payload));
    }

    TEST_CHECK(simrv::device::autodiscover_external_device_plugin(
        "isct,rvcomp-mmc", full_image_path.string(), machine.device_registry()));

    simrv::core::SoCDeviceConfig full_cfg{};
    full_cfg.kind = simrv::core::SoCDeviceKind::CustomMmio;
    full_cfg.name = "rvcomp-mmc";
    full_cfg.compatible = "isct,rvcomp-mmc";
    full_cfg.base = simrv::xlen::PhysAddr{0xa0000000};
    full_cfg.size = 0x2000;
    full_cfg.enabled = true;

    auto full_dev = machine.device_registry().create_mmio(machine, full_cfg);
    TEST_CHECK(full_dev != nullptr);
    TEST_CHECK(full_dev->base_address() == 0xa0000000);
    TEST_CHECK(full_dev->size() == 0x2000);

    // Guest driver requests partition block index 8192 (0x2000 = 32 MiB / 4096)
    full_dev->write32(simrv::memory::MmioOffset{0}, 8192);
    TEST_CHECK(full_dev->read32(simrv::memory::MmioOffset{0}) == 8192);
    TEST_CHECK(full_dev->read32(simrv::memory::MmioOffset{0x1000}) == 0x11223344U);

    full_dev->write32(simrv::memory::MmioOffset{0x1000}, 0x55667788U);
    full_dev->write32(simrv::memory::MmioOffset{0}, 8193);

    {
        std::ifstream img(full_image_path, std::ios::binary);
        img.seekg(0x2000000ULL);
        uint32_t readback = 0;
        img.read(reinterpret_cast<char*>(&readback), sizeof(readback));
        TEST_CHECK(readback == 0x55667788U);
    }
    full_dev.reset();
    std::error_code ec;
    std::filesystem::remove(full_image_path, ec);

    // 2. Raw partition image test (< 32 MiB, where 8192 maps to block 0)
    const auto raw_image_path = std::filesystem::temp_directory_path() / "simrv-rvcomp-mmc-raw.img";
    {
        std::ofstream img(raw_image_path, std::ios::binary | std::ios::trunc);
        img.seekp(65536 - 1);
        img.put('\0');
        img.seekp(0x438);
        const uint16_t magic = 0xef53;
        img.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        img.seekp(0);
        const uint32_t payload = 0xaabbccddU;
        img.write(reinterpret_cast<const char*>(&payload), sizeof(payload));
    }

    simrv::core::Machine raw_machine(simrv::core::MachineConfig{
        .soc = simrv::core::SoCConfig::rvcomp(),
    });
    TEST_CHECK(simrv::device::autodiscover_external_device_plugin(
        "isct,rvcomp-mmc", raw_image_path.string(), raw_machine.device_registry()));

    auto raw_dev = raw_machine.device_registry().create_mmio(raw_machine, full_cfg);
    TEST_CHECK(raw_dev != nullptr);
    raw_dev->write32(simrv::memory::MmioOffset{0}, 8192);
    TEST_CHECK(raw_dev->read32(simrv::memory::MmioOffset{0x1000}) == 0xaabbccddU);

    raw_dev->write32(simrv::memory::MmioOffset{0x1000}, 0x12344321U);
    raw_dev->write32(simrv::memory::MmioOffset{0}, 8193);

    {
        std::ifstream img(raw_image_path, std::ios::binary);
        img.seekg(0);
        uint32_t readback = 0;
        img.read(reinterpret_cast<char*>(&readback), sizeof(readback));
        TEST_CHECK(readback == 0x12344321U);
    }
    raw_dev.reset();
    std::filesystem::remove(raw_image_path, ec);
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
#if defined(SIMRV_TEST_RVCOMP_UART)
    test_rvcomp_uart_plugin();
#endif
    test_rvcomp_mmc_plugin_and_device();
    std::cout << "All RVComp profile tests passed successfully." << std::endl;
    return 0;
}
