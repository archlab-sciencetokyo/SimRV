#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

#include "TestHelper.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/CpuConfigParser.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/MachineConfig.hpp"
#include "simrv/core/TelemetrySink.hpp"
#include "simrv/device/ExternalDevicePlugin.hpp"
#include "simrv/device/ExternalFramebufferDevice.h"
#include "simrv/device/ExternalSerialDevice.h"
#include "simrv/device/FramebufferDevice.hpp"
#include "simrv/device/SerialDevice.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/TileLinkBus.hpp"
#include "simrv/util/Scaffolding.hpp"

static_assert(std::is_standard_layout_v<SimRvSerialHostServices>);
static_assert(std::is_standard_layout_v<SimRvExternalSerialDevice>);
static_assert(SIMRV_EXTERNAL_SERIAL_API_VERSION == 1);
static_assert(std::is_standard_layout_v<SimRvExternalFramebufferDevice>);
static_assert(std::is_standard_layout_v<SimRvHostServices>);
static_assert(std::is_standard_layout_v<SimRvExternalDevice>);
static_assert(SIMRV_EXTERNAL_DEVICE_API_VERSION == 2);
static_assert(SIMRV_EXTERNAL_DEVICE_API_VERSION_V1 == 1);

void test_plugin_host_services_are_per_device() {
    struct Context {
        const SimRvHostServices* services = nullptr;
    } first, second;
    SimRvExternalDevice descriptor{};
    descriptor.set_host_services = [](void* ctx, const SimRvHostServices* services) {
        static_cast<Context*>(ctx)->services = services;
    };
    simrv::core::Machine machine1{};
    simrv::core::Machine machine2{};
    simrv::device::ExternalMmioDevice dev1(&descriptor, &first, "first",
                                           simrv::xlen::PhysAddr{0x1000}, 0x100, machine1);
    simrv::device::ExternalMmioDevice dev2(&descriptor, &second, "second",
                                           simrv::xlen::PhysAddr{0x2000}, 0x100, machine2);
    TEST_CHECK(first.services != nullptr && second.services != nullptr);
    TEST_CHECK(first.services != second.services);
    TEST_CHECK(first.services->host_ctx == &machine1);
    TEST_CHECK(second.services->host_ctx == &machine2);
    second.services->set_irq(second.services->host_ctx, 3, 1);
    TEST_CHECK((machine1.primary_hart().plic_mmio.plic_pending[0] & (1U << 3)) == 0);
    TEST_CHECK((machine2.primary_hart().plic_mmio.plic_pending[0] & (1U << 3)) != 0);
}

void test_serial_plugin_replaces_builtin() {
    struct Capture final : simrv::core::IConsoleSink {
        std::string output;
        void handle_char_write(char ch) override { output.push_back(ch); }
    };
    simrv::core::MachineConfig cfg{};
    TEST_CHECK(simrv::device::resolve_device_plugin_path("vendor-test-serial-plugin").has_value());
    cfg.tui.enabled = true;
    auto uart = simrv::core::soc_device_default(simrv::core::SoCDeviceKind::Uart);
    uart.compatible = "test,serial";
    cfg.soc.devices.push_back(uart);
    cfg.files.device_plugins.push_back(TEST_SERIAL_PLUGIN_PATH);
    simrv::core::Machine machine(cfg);
    auto capture = std::make_shared<Capture>();
    machine.set_console_sink(capture);
    TEST_CHECK(machine.initialize().has_value());
    auto* serial = machine.uart_device();
    TEST_CHECK(serial != nullptr);
    simrv::memory::TlChannelA reset_check{};
    simrv::memory::TlChannelD reset_response{};
    reset_check.address = simrv::xlen::PhysAddr{uart.base + 4};
    reset_check.size = 0;
    reset_check.opcode = simrv::memory::TlOpcodeA::Get;
    TEST_CHECK(serial->handle_request(reset_check, reset_response));
    TEST_CHECK(reset_response.data == 1);
    reset_check.address = simrv::xlen::PhysAddr{uart.base + 8};
    TEST_CHECK(serial->handle_request(reset_check, reset_response));
    TEST_CHECK(reset_response.denied);
    reset_check.address = simrv::xlen::PhysAddr{uart.base + 12};
    TEST_CHECK(serial->handle_request(reset_check, reset_response));
    TEST_CHECK(!reset_response.denied && reset_response.data == 0x34);
    serial->push_rx_byte('K');
    TEST_CHECK(serial->is_interrupt_pending());
    serial->advance_cycles(1);
    TEST_CHECK((machine.primary_hart().plic_mmio.plic_pending[0] & (1U << uart.irq)) != 0);
    simrv::memory::TlChannelA request{};
    simrv::memory::TlChannelD response{};
    request.address = simrv::xlen::PhysAddr{uart.base};
    request.size = 0;
    request.opcode = simrv::memory::TlOpcodeA::Get;
    TEST_CHECK(serial->handle_request(request, response));
    TEST_CHECK(response.data == 'K');
    serial->service_interrupts();
    TEST_CHECK(!serial->is_interrupt_pending());
    request.opcode = simrv::memory::TlOpcodeA::PutFullData;
    request.data = 'X';
    TEST_CHECK(serial->handle_request(request, response));
    TEST_CHECK(capture->output == "X");
}

void test_serial_plugin_device_ticks_in_fast_baremetal() {
    const auto path =
        std::filesystem::temp_directory_path() /
        ("simrv-serial-guest-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin");
    {
        std::ofstream out(path, std::ios::binary);
        const uint32_t loop = 0x0000006f;  // jal x0, 0
        out.write(reinterpret_cast<const char*>(&loop), sizeof(loop));
    }
    simrv::core::MachineConfig cfg{};
    cfg.memory.dram_size = 0x100000;
    cfg.execution.start_pc = simrv::xlen::VirtAddr{simrv::memory::kDramBaseAddress.raw()};
    cfg.execution.fincnt = 128;
    cfg.files.binary_path = path.string();
    auto uart = simrv::core::soc_device_default(simrv::core::SoCDeviceKind::Uart);
    uart.compatible = "test,serial";
    cfg.soc.devices.push_back(uart);
    cfg.files.device_plugins.push_back(TEST_SERIAL_PLUGIN_PATH);
    simrv::core::Machine machine(cfg);
    TEST_CHECK(machine.initialize().has_value());
    machine.run();
    TEST_CHECK(machine.retired_instruction_count() == 128);
    simrv::memory::TlChannelA request{};
    simrv::memory::TlChannelD response{};
    request.address = simrv::xlen::PhysAddr{uart.base + 24};
    request.size = 2;
    request.opcode = simrv::memory::TlOpcodeA::Get;
    TEST_CHECK(machine.uart_device()->handle_request(request, response));
    TEST_CHECK(!response.denied && response.data == 128);
    std::error_code error;
    std::filesystem::remove(path, error);
}

void test_mmio_plugin_replaces_standalone_builtin() {
    simrv::core::MachineConfig cfg{};
    cfg.tui.enabled = true;
    auto power = simrv::core::soc_device_default(simrv::core::SoCDeviceKind::Power);
    power.compatible = "test,serial";
    cfg.soc.devices.push_back(power);
    cfg.files.device_plugins.push_back(TEST_SERIAL_PLUGIN_PATH);
    simrv::core::Machine machine(cfg);
    TEST_CHECK(machine.initialize().has_value());
    auto* node =
        machine.memory().system_bus().router().resolve_device(simrv::xlen::PhysAddr{power.base});
    auto* replacement = dynamic_cast<simrv::memory::MmioDevice*>(node);
    TEST_CHECK(replacement != nullptr);
    TEST_CHECK(replacement->read32(0) == 0xffffffffU);
}

void test_framebuffer_uses_mmio_device_context() {
    simrv::core::Machine machine{};
    simrv::device::PlatformDeviceRegistry registry;
    TEST_CHECK(simrv::device::load_external_device_plugin(TEST_SERIAL_PLUGIN_PATH, registry));
    simrv::core::SoCDeviceConfig config{};
    config.compatible = "test,serial";
    auto mmio = registry.create_mmio(machine, config);
    TEST_CHECK(mmio != nullptr);
    auto* framebuffer = dynamic_cast<simrv::device::FramebufferDevice*>(mmio.get());
    TEST_CHECK(framebuffer != nullptr);
    TEST_CHECK(framebuffer->framebuffer_data().size() == 4);
    mmio->write8(16, 0x5a);
    TEST_CHECK(framebuffer->framebuffer_data()[0] == 0x5a);

    simrv::core::MachineConfig cfg{};
    cfg.tui.enabled = true;
    config.kind = simrv::core::SoCDeviceKind::CustomMmio;
    config.name = "test-framebuffer";
    config.base = simrv::xlen::PhysAddr{0x10010000};
    config.size = 0x100;
    config.enabled = true;
    cfg.soc.devices.push_back(config);
    cfg.files.device_plugins.push_back(TEST_SERIAL_PLUGIN_PATH);
    simrv::core::Machine configured(cfg);
    TEST_CHECK(configured.initialize().has_value());
    auto* configured_fb = configured.framebuffer_device();
    TEST_CHECK(configured_fb != nullptr);
    auto* node = configured.memory().system_bus().router().resolve_device(
        simrv::xlen::PhysAddr{config.base});
    auto* configured_mmio = dynamic_cast<simrv::memory::MmioDevice*>(node);
    TEST_CHECK(configured_mmio != nullptr);
    configured_mmio->write8(16, 0xa5);
    TEST_CHECK(configured_fb->framebuffer_data()[0] == 0xa5);
    TEST_CHECK(configured.framebuffer_view().size() == 4);
    configured_mmio->write8(20, 1);
    TEST_CHECK(configured.framebuffer_view().data() == nullptr);
}

void test_replacement_creation_failure_is_not_silently_ignored() {
    simrv::core::MachineConfig cfg{};
    cfg.tui.enabled = true;
    auto power = simrv::core::soc_device_default(simrv::core::SoCDeviceKind::Power);
    power.compatible = "test,failing-device";
    cfg.soc.devices.push_back(power);
    simrv::core::Machine machine(cfg);
    TEST_CHECK(machine.device_registry().register_mmio_compatible(
        power.compatible,
        [](simrv::core::Machine&, const simrv::core::SoCDeviceConfig&)
            -> std::unique_ptr<simrv::memory::MmioDevice> { return {}; }));
    TEST_CHECK(!machine.initialize().has_value());

    simrv::core::MachineConfig serial_cfg{};
    serial_cfg.tui.enabled = true;
    auto uart = simrv::core::soc_device_default(simrv::core::SoCDeviceKind::Uart);
    uart.compatible = "test,missing-serial";
    serial_cfg.soc.devices.push_back(uart);
    simrv::core::Machine missing_serial(serial_cfg);
    TEST_CHECK(!missing_serial.initialize().has_value());

    simrv::core::MachineConfig power_cfg{};
    power_cfg.tui.enabled = true;
    power.compatible = "test,missing-power";
    power_cfg.soc.devices.push_back(power);
    simrv::core::Machine missing_power(power_cfg);
    TEST_CHECK(!missing_power.initialize().has_value());
}

void test_capability_registration_is_atomic() {
    simrv::device::PlatformDeviceRegistry registry;
    TEST_CHECK(registry.register_serial_compatible(
        "test,serial",
        [](simrv::core::Machine&, const simrv::core::SoCDeviceConfig&)
            -> std::unique_ptr<simrv::device::SerialDevice> { return {}; }));
    TEST_CHECK(!simrv::device::load_external_device_plugin(TEST_SERIAL_PLUGIN_PATH, registry));
    TEST_CHECK(registry.has_serial_compatible("test,serial"));
    TEST_CHECK(!registry.has_mmio_compatible("test,serial"));
}

void test_v2_host_services_dma_and_logging() {
    struct DeviceState {
        const SimRvHostServices* services = nullptr;
    } dev_state;

    SimRvExternalDevice descriptor{};
    descriptor.api_version = SIMRV_EXTERNAL_DEVICE_API_VERSION;
    descriptor.name = "v2-dma-device";
    descriptor.compatible = "test,v2-dma";
    descriptor.default_base = 0x20000000;
    descriptor.default_size = 0x1000;
    descriptor.set_host_services = [](void* ctx, const SimRvHostServices* services) {
        static_cast<DeviceState*>(ctx)->services = services;
    };

    simrv::core::MachineConfig cfg{};
    cfg.memory.dram_size = 0x100000;  // 1 MiB
    simrv::core::Machine machine(cfg);
    TEST_CHECK(machine.initialize().has_value());

    simrv::device::ExternalMmioDevice mmio_dev(
        &descriptor, &dev_state, "v2-dma-device",
        simrv::xlen::PhysAddr{static_cast<simrv::xlen::Address>(descriptor.default_base)},
        descriptor.default_size, machine);

    TEST_CHECK(dev_state.services != nullptr);
    TEST_CHECK(dev_state.services->dma_read != nullptr);
    TEST_CHECK(dev_state.services->dma_write != nullptr);
    TEST_CHECK(dev_state.services->get_cycle_count != nullptr);
    TEST_CHECK(dev_state.services->log_msg != nullptr);

    // Test structured logging callback does not crash
    dev_state.services->log_msg(dev_state.services->host_ctx, SIMRV_LOG_INFO,
                                "Testing v2 log message");
    dev_state.services->log_msg(dev_state.services->host_ctx, SIMRV_LOG_DEBUG,
                                "Testing v2 debug message");

    // Test cycle query
    const uint64_t initial_cycles =
        dev_state.services->get_cycle_count(dev_state.services->host_ctx);
    TEST_CHECK(initial_cycles == 0);

    // Test DMA write and DMA read to guest RAM (DRAM starts at 0x80000000)
    const uint64_t ram_addr = 0x80001000ULL;
    const std::vector<uint8_t> test_data = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x23, 0x45, 0x67};
    int write_res = dev_state.services->dma_write(dev_state.services->host_ctx, ram_addr,
                                                  test_data.data(), test_data.size());
    TEST_CHECK(write_res == 0);

    std::vector<uint8_t> read_back(test_data.size(), 0);
    int read_res = dev_state.services->dma_read(dev_state.services->host_ctx, ram_addr,
                                                read_back.data(), read_back.size());
    TEST_CHECK(read_res == 0);
    TEST_CHECK(read_back == test_data);

    // A 64-bit plugin address must not truncate to an in-range native RV32 address.
    const uint64_t high_addr = ram_addr | (uint64_t{1} << 32);
    const uint8_t replacement = 0x42;
    TEST_CHECK(dev_state.services->dma_write(dev_state.services->host_ctx, high_addr, &replacement,
                                             sizeof(replacement)) != 0);
    TEST_CHECK(dev_state.services->dma_read(dev_state.services->host_ctx, high_addr,
                                            read_back.data(), read_back.size()) != 0);
    TEST_CHECK(dev_state.services->dma_read(dev_state.services->host_ctx, ram_addr,
                                            read_back.data(), read_back.size()) == 0);
    TEST_CHECK(read_back == test_data);
}

void test_v2_cycle_ticking() {
    struct TickedState {
        uint64_t cycles_received = 0;
    } state;

    SimRvExternalDevice descriptor{};
    descriptor.api_version = SIMRV_EXTERNAL_DEVICE_API_VERSION;
    descriptor.name = "ticked-device";
    descriptor.compatible = "test,ticked";
    descriptor.default_base = 0xa0010000;
    descriptor.default_size = 0x1000;
    descriptor.advance_cycles = [](void* ctx, uint64_t cycles) {
        static_cast<TickedState*>(ctx)->cycles_received += cycles;
    };

    simrv::core::MachineConfig cfg{};
    cfg.tui.enabled = true;
    simrv::core::SoCDeviceConfig soc_dev{};
    soc_dev.kind = simrv::core::SoCDeviceKind::CustomMmio;
    soc_dev.name = "ticked-device";
    soc_dev.compatible = "test,ticked";
    soc_dev.base =
        simrv::xlen::PhysAddr{static_cast<simrv::xlen::Address>(descriptor.default_base)};
    soc_dev.size = descriptor.default_size;
    soc_dev.enabled = true;
    cfg.soc.devices.push_back(soc_dev);

    simrv::core::Machine machine(cfg);
    TEST_CHECK(machine.device_registry().register_mmio_compatible(
        soc_dev.compatible,
        [&descriptor, &state](simrv::core::Machine& m, const simrv::core::SoCDeviceConfig& c)
            -> std::unique_ptr<simrv::memory::MmioDevice> {
            return std::make_unique<simrv::device::ExternalMmioDevice>(
                &descriptor, &state, c.name, simrv::xlen::PhysAddr{c.base}, c.size, m);
        }));

    TEST_CHECK(machine.initialize().has_value());

    // Advance device cycles through machine
    machine.advance_device_cycles(100);
    TEST_CHECK(state.cycles_received == 100);

    machine.advance_device_cycles(55);
    TEST_CHECK(state.cycles_received == 155);
}

void test_v1_compatibility() {
    SimRvExternalDevice descriptor{};
    descriptor.api_version = SIMRV_EXTERNAL_DEVICE_API_VERSION_V1;
    descriptor.name = "v1-device";
    descriptor.compatible = "test,v1";
    descriptor.default_base = 0xa0020000;
    descriptor.default_size = 0x100;

    simrv::core::Machine machine{};
    simrv::device::ExternalMmioDevice mmio_dev(
        &descriptor, nullptr, "v1-device",
        simrv::xlen::PhysAddr{static_cast<simrv::xlen::Address>(descriptor.default_base)},
        descriptor.default_size, machine);

    TEST_CHECK(!mmio_dev.has_cycle_ticking());

    simrv::device::PlatformDeviceRegistry registry;
    TEST_CHECK(
        simrv::device::load_external_device_plugin(TEST_LEGACY_DEVICE_PLUGIN_PATH, registry));
    simrv::core::SoCDeviceConfig config{};
    config.compatible = "test,legacy-device";
    auto legacy = registry.create_mmio(machine, config);
    TEST_CHECK(legacy != nullptr);
    TEST_CHECK(legacy->read32(4) == 0x12340004);
    TEST_CHECK(legacy->read32(8) == 1);
    legacy->write32(0, 1);
    TEST_CHECK((machine.primary_hart().plic_mmio.plic_pending[0] & (1U << 7)) != 0);
    auto* external = dynamic_cast<simrv::device::ExternalMmioDevice*>(legacy.get());
    TEST_CHECK(external != nullptr && !external->has_cycle_ticking());
}

#ifdef TEST_TIMER_PLUGIN_PATH
void test_example_timer_plugin_integration() {
    simrv::core::MachineConfig cfg{};
    cfg.tui.enabled = true;
    simrv::core::SoCDeviceConfig dev{};
    dev.kind = simrv::core::SoCDeviceKind::CustomMmio;
    dev.name = "demo-timer";
    dev.compatible = "simrv,demo-timer";
    dev.base = simrv::xlen::PhysAddr{0x10030000};
    dev.size = 0x1000;
    dev.enabled = true;
    cfg.soc.devices.push_back(dev);
    cfg.files.device_plugins.push_back(TEST_TIMER_PLUGIN_PATH);
    simrv::core::Machine machine(cfg);
    TEST_CHECK(machine.initialize().has_value());

    auto* node =
        machine.memory().system_bus().router().resolve_device(simrv::xlen::PhysAddr{0x10030000});
    auto* mmio = dynamic_cast<simrv::memory::MmioDevice*>(node);
    TEST_CHECK(mmio != nullptr);

    // Write limit = 50 cycles
    mmio->write32(0x08, 50);
    // Write control = Enable (1) | IRQ Enable (2)
    mmio->write32(0x00, 3);

    // Initial status: not expired
    TEST_CHECK(mmio->read32(0x04) == 0);

    // Advance 30 cycles
    machine.advance_device_cycles(30);
    TEST_CHECK(mmio->read32(0x0C) == 30);
    TEST_CHECK(mmio->read32(0x04) == 0);

    // Advance 25 more cycles (total 55 >= 50)
    machine.advance_device_cycles(25);
    // Status should be expired (bit 0 set)
    TEST_CHECK((mmio->read32(0x04) & 1) != 0);

    // Clear status (W1C)
    mmio->write32(0x04, 1);
    TEST_CHECK(mmio->read32(0x04) == 0);
}

void test_timer_plugin_advances_in_fast_baremetal() {
    const auto path =
        std::filesystem::temp_directory_path() /
        ("simrv-timer-guest-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bin");
    {
        std::ofstream out(path, std::ios::binary);
        const uint32_t loop = 0x0000006f;  // jal x0, 0
        out.write(reinterpret_cast<const char*>(&loop), sizeof(loop));
    }
    simrv::core::MachineConfig cfg{};
    cfg.memory.dram_size = 0x100000;
    cfg.execution.start_pc = simrv::xlen::VirtAddr{simrv::memory::kDramBaseAddress.raw()};
    cfg.execution.fincnt = 128;
    cfg.files.binary_path = path.string();
    simrv::core::SoCDeviceConfig dev{};
    dev.kind = simrv::core::SoCDeviceKind::CustomMmio;
    dev.name = "demo-timer";
    dev.compatible = "simrv,demo-timer";
    dev.base = simrv::xlen::PhysAddr{0x10030000};
    dev.size = 0x1000;
    dev.enabled = true;
    cfg.soc.devices.push_back(dev);
    cfg.files.device_plugins.push_back(TEST_TIMER_PLUGIN_PATH);
    simrv::core::Machine machine(cfg);
    TEST_CHECK(machine.initialize().has_value());
    auto* node =
        machine.memory().system_bus().router().resolve_device(simrv::xlen::PhysAddr{0x10030000});
    auto* timer = dynamic_cast<simrv::memory::MmioDevice*>(node);
    TEST_CHECK(timer != nullptr);
    timer->write32(0x08, 1000);
    timer->write32(0x00, 1);
    machine.run();
    TEST_CHECK(machine.retired_instruction_count() == 128);
    TEST_CHECK(timer->read32(0x0c) == 128);
    std::error_code error;
    std::filesystem::remove(path, error);
}
#endif

#ifdef TEST_DMA_PLUGIN_PATH
void test_example_dma_accel_plugin_integration() {
    simrv::core::MachineConfig cfg{};
    cfg.tui.enabled = true;
    cfg.memory.dram_size = 0x100000;
    simrv::core::SoCDeviceConfig dev{};
    dev.kind = simrv::core::SoCDeviceKind::CustomMmio;
    dev.name = "demo-dma-accel";
    dev.compatible = "simrv,demo-dma-accel";
    dev.base = simrv::xlen::PhysAddr{0x10040000};
    dev.size = 0x1000;
    dev.enabled = true;
    cfg.soc.devices.push_back(dev);
    cfg.files.device_plugins.push_back(TEST_DMA_PLUGIN_PATH);
    simrv::core::Machine machine(cfg);
    TEST_CHECK(machine.initialize().has_value());

    auto* node =
        machine.memory().system_bus().router().resolve_device(simrv::xlen::PhysAddr{0x10040000});
    auto* mmio = dynamic_cast<simrv::memory::MmioDevice*>(node);
    TEST_CHECK(mmio != nullptr);

    // Setup memory in guest RAM
    const uint64_t src_ram = 0x80002000ULL;
    const uint64_t dst_ram = 0x80003000ULL;
    const std::vector<uint8_t> input = {10, 20, 30, 40};
    TEST_CHECK(
        machine.memory().dma_write(simrv::xlen::PhysAddr{src_ram}, input.data(), input.size()));

    mmio->write32(0x08, static_cast<uint32_t>(src_ram & 0xFFFFFFFFU));
    mmio->write32(0x0C, static_cast<uint32_t>(src_ram >> 32U));
    mmio->write32(0x10, static_cast<uint32_t>(dst_ram & 0xFFFFFFFFU));
    mmio->write32(0x14, static_cast<uint32_t>(dst_ram >> 32U));
    mmio->write32(0x18, static_cast<uint32_t>(input.size()));

    // Control: Start (1) | Invert (4)
    mmio->write32(0x00, 1 | 4);

    // Verify status done (bit 0 set)
    TEST_CHECK((mmio->read32(0x04) & 1) != 0);

    // Read back inverted data from dst_ram
    std::vector<uint8_t> output(input.size());
    TEST_CHECK(
        machine.memory().dma_read(simrv::xlen::PhysAddr{dst_ram}, output.data(), output.size()));
    for (size_t i = 0; i < input.size(); ++i) {
        TEST_CHECK(output[i] == static_cast<uint8_t>(~input[i]));
    }
}
#endif

void test_scaffolding_and_formalized_configs() {
    namespace fs = std::filesystem;
    simrv::test::ScopedTempDir tmp_dev_dir{"simrv_test_scaffold_device"};
    simrv::test::ScopedTempDir tmp_cfu_dir{"simrv_test_scaffold_cfu"};

    // 1. Scaffold device plugin
    TEST_CHECK(simrv::util::scaffold_device_plugin("my_accel", tmp_dev_dir.string()));
    TEST_CHECK(fs::exists(tmp_dev_dir / "my_accel_plugin.c"));
    TEST_CHECK(fs::exists(tmp_dev_dir / "CMakeLists.txt"));
    TEST_CHECK(fs::exists(tmp_dev_dir / "README.md"));
    TEST_CHECK(fs::exists(tmp_dev_dir / "my_accel.cfg"));

    // Verify generated SoC config is valid and parsed properly
    {
        simrv::pipeline::CpuModelConfig cpu_model{};
        simrv::core::SoCConfig soc_config{};
        TEST_CHECK(
            simrv::core::load_platform_config(tmp_dev_dir / "my_accel.cfg", cpu_model, soc_config));
        TEST_CHECK(!soc_config.plugins.empty());
        TEST_CHECK(soc_config.plugins[0].find("my_accel") != std::string::npos);
        TEST_CHECK(!soc_config.devices.empty());
        TEST_CHECK(soc_config.devices[0].kind == simrv::core::SoCDeviceKind::CustomMmio);
        TEST_CHECK(soc_config.devices[0].name == "my_accel");
        TEST_CHECK(soc_config.devices[0].base == 0x10050000);
    }

    // 2. Scaffold CFU plugin
    TEST_CHECK(simrv::util::scaffold_cfu_plugin("my_cfu", tmp_cfu_dir.string()));
    TEST_CHECK(fs::exists(tmp_cfu_dir / "my_cfu_plugin.c"));
    TEST_CHECK(fs::exists(tmp_cfu_dir / "CMakeLists.txt"));
    TEST_CHECK(fs::exists(tmp_cfu_dir / "README.md"));
    TEST_CHECK(fs::exists(tmp_cfu_dir / "my_cfu.cfg"));

    // Verify generated CFU config is valid and parsed properly
    {
        simrv::pipeline::CpuModelConfig cpu_model{};
        simrv::core::SoCConfig soc_config{};
        TEST_CHECK(
            simrv::core::load_platform_config(tmp_cfu_dir / "my_cfu.cfg", cpu_model, soc_config));
        TEST_CHECK(cpu_model.cfu.enabled);
        TEST_CHECK(cpu_model.cfu.opcode == "custom-0");
        TEST_CHECK(cpu_model.cfu.plugin.find("my_cfu") != std::string::npos);
        TEST_CHECK(cpu_model.cfu.default_latency == 1);
    }
}

int main() {
    test_plugin_host_services_are_per_device();
    test_serial_plugin_replaces_builtin();
    test_serial_plugin_device_ticks_in_fast_baremetal();
    test_mmio_plugin_replaces_standalone_builtin();
    test_framebuffer_uses_mmio_device_context();
    test_replacement_creation_failure_is_not_silently_ignored();
    test_capability_registration_is_atomic();
    test_v2_host_services_dma_and_logging();
    test_v2_cycle_ticking();
    test_v1_compatibility();
    test_scaffolding_and_formalized_configs();
#ifdef TEST_TIMER_PLUGIN_PATH
    test_example_timer_plugin_integration();
    test_timer_plugin_advances_in_fast_baremetal();
#endif
#ifdef TEST_DMA_PLUGIN_PATH
    test_example_dma_accel_plugin_integration();
#endif
    std::cout << "Device plugin tests passed\n";
}
