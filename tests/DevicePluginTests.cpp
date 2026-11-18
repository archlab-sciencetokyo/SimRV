#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "TestHelper.hpp"
#include "simrv/SafetyProfiles.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/CpuConfigParser.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/MachineConfig.hpp"
#include "simrv/core/TelemetrySink.hpp"
#include "simrv/debug/SymbolTable.hpp"
#include "simrv/device/ExternalDevicePlugin.hpp"
#include "simrv/device/ExternalFramebufferDevice.h"
#include "simrv/device/ExternalInputDevice.h"
#include "simrv/device/ExternalSerialDevice.h"
#include "simrv/device/FramebufferDevice.hpp"
#include "simrv/device/InputDevice.hpp"
#include "simrv/device/RingBufferEthernetMmio.hpp"
#include "simrv/device/SerialDevice.hpp"
#include "simrv/device/pci/PciDevice.hpp"
#include "simrv/device/pci/PcieRootComplex.hpp"
#include "simrv/device/pflash/PFlash.hpp"
#include "simrv/device/virtio/HostAudioSink.hpp"
#include "simrv/device/virtio/VirtioCore.hpp"
#include "simrv/device/virtio/VirtioRngQueue.hpp"
#include "simrv/device/virtio/VirtioSoundBackend.hpp"
#include "simrv/memory/Bus.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/MmioRouter.hpp"
#include "simrv/util/ElfTypes.hpp"
#include "simrv/util/Scaffolding.hpp"

static_assert(std::is_standard_layout_v<SimRvSerialHostServices>);
static_assert(std::is_standard_layout_v<SimRvExternalSerialDevice>);
static_assert(SIMRV_EXTERNAL_SERIAL_API_VERSION == 1);
static_assert(std::is_standard_layout_v<SimRvExternalFramebufferDevice>);
static_assert(std::is_standard_layout_v<SimRvExternalInputDevice>);
static_assert(SIMRV_EXTERNAL_INPUT_API_VERSION == 1);
static_assert(std::is_standard_layout_v<SimRvHostServices>);
static_assert(std::is_standard_layout_v<SimRvExternalDevice>);
static_assert(SIMRV_EXTERNAL_DEVICE_API_VERSION == 2);
static_assert(SIMRV_EXTERNAL_DEVICE_API_VERSION_V1 == 1);
static_assert(!std::is_copy_constructible_v<simrv::device::ExternalMmioDevice>);
static_assert(!std::is_move_constructible_v<simrv::device::ExternalMmioDevice>);

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
    TEST_CHECK(replacement->read32(simrv::memory::MmioOffset{0}) == 0xffffffffU);
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
    mmio->write8(simrv::memory::MmioOffset{16}, 0x5a);
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
    configured_mmio->write8(simrv::memory::MmioOffset{16}, 0xa5);
    TEST_CHECK(configured_fb->framebuffer_data()[0] == 0xa5);
    TEST_CHECK(configured.framebuffer_view().size() == 4);
    configured_mmio->write8(simrv::memory::MmioOffset{20}, 1);
    TEST_CHECK(configured.framebuffer_view().data() == nullptr);
}

#ifdef TEST_RV98_KEYBOARD_PLUGIN_PATH
void test_external_input_plugin_translates_key_events() {
    simrv::core::MachineConfig machine_config{};
    machine_config.files.device_plugins.push_back(TEST_RV98_KEYBOARD_PLUGIN_PATH);
    simrv::core::SoCDeviceConfig config{};
    config.kind = simrv::core::SoCDeviceKind::CustomMmio;
    config.name = "keyboard";
    config.compatible = "example,keyboard";
    config.base = simrv::xlen::PhysAddr{0x1d000000};
    config.size = 0x1000;
    machine_config.soc.devices.push_back(config);
    simrv::core::Machine machine{machine_config};
    TEST_CHECK(machine.initialize().has_value());

    auto* mmio = dynamic_cast<simrv::memory::MmioDevice*>(
        machine.memory().system_bus().router().resolve_device(config.base));
    TEST_CHECK(mmio != nullptr);
    auto* input = dynamic_cast<simrv::device::InputDevice*>(mmio);
    TEST_CHECK(input != nullptr);

    input->send_key(30, true);  // KEY_A make code.
    TEST_CHECK(mmio->read8(simrv::memory::MmioOffset{0}) == 0x1c);
    input->send_key(30, false);
    TEST_CHECK(mmio->read8(simrv::memory::MmioOffset{0}) == 0xf0);
    TEST_CHECK(mmio->read8(simrv::memory::MmioOffset{0}) == 0x1c);

    input->send_key(103, true);  // KEY_UP uses an extended scan code.
    TEST_CHECK(mmio->read8(simrv::memory::MmioOffset{0}) == 0xe0);
    TEST_CHECK(mmio->read8(simrv::memory::MmioOffset{0}) == 0x75);
    input->send_key(103, false);
    TEST_CHECK(mmio->read8(simrv::memory::MmioOffset{0}) == 0xe0);
    TEST_CHECK(mmio->read8(simrv::memory::MmioOffset{0}) == 0xf0);
    TEST_CHECK(mmio->read8(simrv::memory::MmioOffset{0}) == 0x75);
    input->send_key(0xffff, true);  // Unknown Linux keycodes are ignored.
    TEST_CHECK(mmio->read8(simrv::memory::MmioOffset{0}) == 0);
}
#endif

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
    TEST_CHECK(legacy->read32(simrv::memory::MmioOffset{4}) == 0x12340004);
    TEST_CHECK(legacy->read32(simrv::memory::MmioOffset{8}) == 1);
    legacy->write32(simrv::memory::MmioOffset{0}, 1);
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
    mmio->write32(simrv::memory::MmioOffset{0x08}, 50);
    // Write control = Enable (1) | IRQ Enable (2)
    mmio->write32(simrv::memory::MmioOffset{0x00}, 3);

    // Initial status: not expired
    TEST_CHECK(mmio->read32(simrv::memory::MmioOffset{0x04}) == 0);

    // Advance 30 cycles
    machine.advance_device_cycles(30);
    TEST_CHECK(mmio->read32(simrv::memory::MmioOffset{0x0C}) == 30);
    TEST_CHECK(mmio->read32(simrv::memory::MmioOffset{0x04}) == 0);

    // Advance 25 more cycles (total 55 >= 50)
    machine.advance_device_cycles(25);
    // Status should be expired (bit 0 set)
    TEST_CHECK((mmio->read32(simrv::memory::MmioOffset{0x04}) & 1) != 0);

    // Clear status (W1C)
    mmio->write32(simrv::memory::MmioOffset{0x04}, 1);
    TEST_CHECK(mmio->read32(simrv::memory::MmioOffset{0x04}) == 0);
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
    timer->write32(simrv::memory::MmioOffset{0x08}, 1000);
    timer->write32(simrv::memory::MmioOffset{0x00}, 1);
    machine.run();
    TEST_CHECK(machine.retired_instruction_count() == 128);
    TEST_CHECK(timer->read32(simrv::memory::MmioOffset{0x0c}) == 128);
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

    mmio->write32(simrv::memory::MmioOffset{0x08}, static_cast<uint32_t>(src_ram & 0xFFFFFFFFU));
    mmio->write32(simrv::memory::MmioOffset{0x0C}, static_cast<uint32_t>(src_ram >> 32U));
    mmio->write32(simrv::memory::MmioOffset{0x10}, static_cast<uint32_t>(dst_ram & 0xFFFFFFFFU));
    mmio->write32(simrv::memory::MmioOffset{0x14}, static_cast<uint32_t>(dst_ram >> 32U));
    mmio->write32(simrv::memory::MmioOffset{0x18}, static_cast<uint32_t>(input.size()));

    // Control: Start (1) | Invert (4)
    mmio->write32(simrv::memory::MmioOffset{0x00}, 1 | 4);

    // Verify status done (bit 0 set)
    TEST_CHECK((mmio->read32(simrv::memory::MmioOffset{0x04}) & 1) != 0);

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

void test_external_mmio_bounds_and_dma_overflow() {
    struct State {
        const SimRvHostServices* services = nullptr;
        uint32_t read_calls = 0;
        uint32_t write_calls = 0;
    } state;

    SimRvExternalDevice descriptor{};
    descriptor.api_version = SIMRV_EXTERNAL_DEVICE_API_VERSION;
    descriptor.name = "bounds-device";
    descriptor.compatible = "test,bounds";
    descriptor.default_base = 0x20010000;
    descriptor.default_size = 0x100;
    descriptor.set_host_services = [](void* ctx, const SimRvHostServices* services) {
        static_cast<State*>(ctx)->services = services;
    };
    descriptor.read = [](void* ctx, uint64_t offset, uint32_t, uint64_t* out_val) -> int {
        auto* st = static_cast<State*>(ctx);
        ++st->read_calls;
        *out_val = 0x12345678ULL + offset;
        return 0;
    };
    descriptor.write = [](void* ctx, uint64_t, uint32_t, uint64_t) -> int {
        ++static_cast<State*>(ctx)->write_calls;
        return 0;
    };

    simrv::core::MachineConfig cfg{};
    cfg.memory.dram_size = 0x100000;
    simrv::core::Machine machine(cfg);
    TEST_CHECK(machine.initialize().has_value());

    simrv::device::ExternalMmioDevice mmio_dev(
        &descriptor, &state, "bounds-device",
        simrv::xlen::PhysAddr{static_cast<simrv::xlen::Address>(descriptor.default_base)},
        descriptor.default_size, machine);

    TEST_CHECK(mmio_dev.read32(simrv::memory::MmioOffset{0xFC}) == 0x12345678U + 0xFCU);
    mmio_dev.write32(simrv::memory::MmioOffset{0xFC}, 0x55);
    TEST_CHECK(state.read_calls == 1 && state.write_calls == 1);

    // Out-of-bounds accesses must return 0 and avoid forwarding past device size.
    TEST_CHECK(mmio_dev.read8(simrv::memory::MmioOffset{0x100}) == 0);
    TEST_CHECK(mmio_dev.read32(simrv::memory::MmioOffset{0xFE}) == 0);
    TEST_CHECK(mmio_dev.read64(simrv::memory::MmioOffset{0xFC}) == 0);
    mmio_dev.write8(simrv::memory::MmioOffset{0x100}, 1);
    mmio_dev.write32(simrv::memory::MmioOffset{0xFE}, 1);
    mmio_dev.write64(simrv::memory::MmioOffset{0xFC}, 1);
    TEST_CHECK(state.read_calls == 1 && state.write_calls == 1);

    simrv::memory::TlChannelA req{};
    simrv::memory::TlChannelD resp{};
    req.address =
        simrv::xlen::PhysAddr{static_cast<simrv::xlen::Address>(descriptor.default_base + 0xFE)};
    req.size = 2;
    req.opcode = simrv::memory::TlOpcodeA::Get;
    TEST_CHECK(mmio_dev.handle_request(req, resp) && resp.denied);
    TEST_CHECK(state.read_calls == 1);

    // DMA callbacks must reject 64-bit address wraparound and null pointers.
    TEST_CHECK(state.services != nullptr);
    std::array<uint8_t, 32> buf{};
    TEST_CHECK(state.services->dma_read(state.services->host_ctx, 0xFFFFFFFFFFFFFFF0ULL, buf.data(),
                                        buf.size()) != 0);
    TEST_CHECK(state.services->dma_write(state.services->host_ctx, 0xFFFFFFFFFFFFFFF0ULL,
                                         buf.data(), buf.size()) != 0);
    TEST_CHECK(state.services->dma_read(state.services->host_ctx, 0x80001000ULL, nullptr, 4) != 0);
    TEST_CHECK(state.services->dma_write(state.services->host_ctx, 0x80001000ULL, nullptr, 4) != 0);
}

void test_ring_buffer_ethernet_subword_and_net_bounds() {
    simrv::core::Machine machine{};
    simrv::device::RingBufferEthernetMmio eth(
        machine, "eth", simrv::xlen::PhysAddr{0x14000000}, 0x1000, 1,
        simrv::xlen::PhysAddr{0x18000000}, 100, simrv::xlen::PhysAddr{0x1c000000}, 100,
        {0x52, 0x54, 0x00, 0x12, 0x34, 0x56}, simrv::device::virtio::NetBackend::Mode::None);

    // Non-power-of-two buffer sizes are sanitized to power-of-two >= 64.
    TEST_CHECK(eth.rx_node()->size() == 64);
    TEST_CHECK(eth.tx_node()->size() == 64);

    auto* tx_mmio = dynamic_cast<simrv::memory::MmioDevice*>(eth.tx_node());
    TEST_CHECK(tx_mmio != nullptr);
    tx_mmio->write8(simrv::memory::MmioOffset{1}, 0xAB);
    tx_mmio->write16(simrv::memory::MmioOffset{2}, 0x1234);
    TEST_CHECK(tx_mmio->read8(simrv::memory::MmioOffset{1}) == 0xAB);
    TEST_CHECK(tx_mmio->read16(simrv::memory::MmioOffset{2}) == 0x1234);
    TEST_CHECK(tx_mmio->read32(simrv::memory::MmioOffset{0}) == 0x1234AB00U);

    // Out-of-bounds buffer accesses are safely ignored.
    tx_mmio->write32(simrv::memory::MmioOffset{62}, 0xDEADBEEFU);
    TEST_CHECK(tx_mmio->read32(simrv::memory::MmioOffset{62}) == 0);

    // NetBackend tracks tx_packet_count without retaining unbounded history and caps rx_queue_.
    const std::array<uint8_t, 60> valid_frame{};
    eth.backend().send_tx_packet(valid_frame);
    TEST_CHECK(eth.backend().tx_packet_count() == 1);
    const std::vector<uint8_t> oversized_frame(
        simrv::device::virtio::NetBackend::kMaxPacketBytes + 1, 0x11);
    eth.backend().send_tx_packet(oversized_frame);
    TEST_CHECK(eth.backend().tx_packet_count() == 1);

    for (std::size_t i = 0; i < simrv::device::virtio::NetBackend::kMaxRxQueuePackets + 25; ++i) {
        std::array<uint8_t, 4> pkt{static_cast<uint8_t>(i & 0xFFU), 0, 0, 0};
        eth.backend().push_rx_packet(pkt);
    }
    std::size_t popped = 0;
    while (eth.backend().has_rx_packet()) {
        (void)eth.backend().pop_rx_packet();
        ++popped;
    }
    TEST_CHECK(popped == simrv::device::virtio::NetBackend::kMaxRxQueuePackets);
}

void test_ring_buffer_ethernet_dhcp_round_trip() {
    simrv::core::Machine machine{};
    constexpr std::array<uint8_t, 6> mac = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    simrv::device::RingBufferEthernetMmio eth(machine, "eth", simrv::xlen::PhysAddr{0x14000000},
                                              0x4000, 2, simrv::xlen::PhysAddr{0x18000000}, 0x4000,
                                              simrv::xlen::PhysAddr{0x1c000000}, 0x2000, mac,
                                              simrv::device::virtio::NetBackend::Mode::User);
    auto* csr = dynamic_cast<simrv::memory::MmioDevice*>(eth.csr_node());
    auto* rx = dynamic_cast<simrv::memory::MmioDevice*>(eth.rx_node());
    auto* tx = dynamic_cast<simrv::memory::MmioDevice*>(eth.tx_node());
    TEST_CHECK(csr != nullptr && rx != nullptr && tx != nullptr);

    constexpr std::size_t kIpOffset = 14;
    constexpr std::size_t kUdpOffset = kIpOffset + 20;
    constexpr std::size_t kBootpOffset = kUdpOffset + 8;
    std::array<uint8_t, kBootpOffset + 244> discover{};
    std::fill_n(discover.begin(), 6, uint8_t{0xFF});
    std::copy(mac.begin(), mac.end(), discover.begin() + 6);
    discover[12] = 0x08;
    discover[13] = 0x00;
    discover[kIpOffset] = 0x45;
    discover[kIpOffset + 2] = 0x01;
    discover[kIpOffset + 3] = 0x10;  // 20-byte IPv4 + 8-byte UDP + 244-byte BOOTP
    discover[kIpOffset + 8] = 64;
    discover[kIpOffset + 9] = 17;
    std::fill(discover.begin() + static_cast<std::ptrdiff_t>(kIpOffset + 16),
              discover.begin() + static_cast<std::ptrdiff_t>(kIpOffset + 20), 0xFF);
    uint32_t checksum = 0;
    for (std::size_t offset = kIpOffset; offset < kIpOffset + 20; offset += 2) {
        checksum += (static_cast<uint32_t>(discover[offset]) << 8U) | discover[offset + 1];
    }
    while ((checksum >> 16U) != 0) checksum = (checksum & 0xFFFFU) + (checksum >> 16U);
    const auto ip_checksum = static_cast<uint16_t>(~checksum);
    discover[kIpOffset + 10] = static_cast<uint8_t>(ip_checksum >> 8U);
    discover[kIpOffset + 11] = static_cast<uint8_t>(ip_checksum);
    discover[kUdpOffset + 1] = 68;
    discover[kUdpOffset + 2] = 0;
    discover[kUdpOffset + 3] = 67;
    discover[kUdpOffset + 4] = 0;
    discover[kUdpOffset + 5] = 0xFC;
    auto* bootp = discover.data() + kBootpOffset;
    bootp[0] = 1;
    bootp[1] = 1;
    bootp[2] = 6;
    bootp[4] = 0x12;
    bootp[5] = 0x34;
    bootp[6] = 0x56;
    bootp[7] = 0x78;
    bootp[10] = 0x80;
    std::copy(mac.begin(), mac.end(), bootp + 28);
    bootp[236] = 0x63;
    bootp[237] = 0x82;
    bootp[238] = 0x53;
    bootp[239] = 0x63;
    bootp[240] = 53;
    bootp[241] = 1;
    bootp[242] = 1;
    bootp[243] = 255;

    const auto aligned_size = (discover.size() + 3U) & ~std::size_t{3};
    tx->write32(simrv::memory::MmioOffset{0}, static_cast<uint32_t>(discover.size()));
    for (std::size_t offset = 0; offset < aligned_size; offset += 4) {
        uint32_t word = 0;
        for (std::size_t byte = 0; byte < 4 && offset + byte < discover.size(); ++byte) {
            word |= static_cast<uint32_t>(discover[offset + byte]) << (byte * 8U);
        }
        tx->write32(simrv::memory::MmioOffset{static_cast<uint32_t>(offset + 4)}, word);
    }
    const auto tx_end = static_cast<uint32_t>(aligned_size + 4);
    for (uint32_t lane = 0; lane < 4; ++lane) {
        csr->write8(simrv::memory::MmioOffset{24 + lane},
                    static_cast<uint8_t>(tx_end >> (lane * 8U)));
    }
    TEST_CHECK(csr->read8(simrv::memory::MmioOffset{24}) == static_cast<uint8_t>(tx_end));
    eth.poll_backend();

    const auto rx_end = csr->read32(simrv::memory::MmioOffset{4});
    TEST_CHECK(rx_end >= discover.size());
    TEST_CHECK((rx->read32(simrv::memory::MmioOffset{12}) & 0xFFFFU) == 0x0008U);
    const auto offer_type_word = rx->read32(simrv::memory::MmioOffset{284});
    TEST_CHECK((offer_type_word & 0xFFU) == 2U);
}

void test_machine_reinitialize_and_reset_cleans_up_devices() {
    struct State {
        uint32_t reset_calls = 0;
        uint64_t ticks = 0;
    } state;

    SimRvExternalDevice descriptor{};
    descriptor.api_version = SIMRV_EXTERNAL_DEVICE_API_VERSION;
    descriptor.name = "reinit-device";
    descriptor.compatible = "test,reinit";
    descriptor.default_base = 0x10060000;
    descriptor.default_size = 0x1000;
    descriptor.reset = [](void* ctx) { ++static_cast<State*>(ctx)->reset_calls; };
    descriptor.read = [](void*, uint64_t, uint32_t, uint64_t* out) {
        *out = 0;
        return 0;
    };
    descriptor.write = [](void*, uint64_t, uint32_t, uint64_t) { return 0; };
    descriptor.advance_cycles = [](void* ctx, uint64_t cycles) {
        static_cast<State*>(ctx)->ticks += cycles;
    };

    simrv::core::MachineConfig cfg{};
    cfg.tui.enabled = true;
    simrv::core::SoCDeviceConfig dev{};
    dev.kind = simrv::core::SoCDeviceKind::CustomMmio;
    dev.name = "reinit-device";
    dev.compatible = "test,reinit";
    dev.base = simrv::xlen::PhysAddr{0x10060000};
    dev.size = 0x1000;
    dev.enabled = true;
    cfg.soc.devices.push_back(dev);

    simrv::core::Machine machine(cfg);
    TEST_CHECK(machine.device_registry().register_mmio_compatible(
        dev.compatible,
        [&descriptor, &state](simrv::core::Machine& m, const simrv::core::SoCDeviceConfig& c)
            -> std::unique_ptr<simrv::memory::MmioDevice> {
            return std::make_unique<simrv::device::ExternalMmioDevice>(
                &descriptor, &state, c.name, simrv::xlen::PhysAddr{c.base}, c.size, m);
        }));

    TEST_CHECK(machine.initialize().has_value());
    TEST_CHECK(state.reset_calls == 1);
    TEST_CHECK(machine.initialize().has_value());
    TEST_CHECK(state.reset_calls == 2);

    // Re-initialization must replace, not duplicate, ticked extension devices.
    machine.advance_device_cycles(10);
    TEST_CHECK(state.ticks == 10);

    machine.reset_state();
    TEST_CHECK(state.reset_calls == 3);
}

void test_memory_safety_audit_and_cxx_profiles() {
    namespace fs = std::filesystem;
    using namespace simrv::device;
    using namespace simrv::device::virtio;

    // 1. simrv::profiles helpers: checked_subspan and unaligned scalar load/store.
    std::array<std::byte, 8> raw_bytes{};
    auto valid_slice = simrv::profiles::checked_subspan(std::span<std::byte>(raw_bytes), 2, 4);
    TEST_CHECK(valid_slice.has_value() && valid_slice->size() == 4);
    TEST_CHECK(!simrv::profiles::checked_subspan(std::span<std::byte>(raw_bytes),
                                                 std::numeric_limits<std::size_t>::max(), 4)
                    .has_value());
    TEST_CHECK(
        !simrv::profiles::checked_subspan(std::span<std::byte>(raw_bytes), 6, 4).has_value());

    TEST_CHECK(simrv::profiles::write_unaligned<uint32_t>(std::span<std::byte>(raw_bytes), 1,
                                                          0x11223344U));
    const auto unaligned_val =
        simrv::profiles::read_unaligned<uint32_t>(std::span<const std::byte>(raw_bytes), 1);
    TEST_CHECK(unaligned_val.has_value() && *unaligned_val == 0x11223344U);
    TEST_CHECK(!simrv::profiles::write_unaligned<uint32_t>(std::span<std::byte>(raw_bytes), 6, 1U));
    TEST_CHECK(!simrv::profiles::read_unaligned<uint32_t>(std::span<const std::byte>(raw_bytes), 6)
                    .has_value());

    // 2. RamView span/subspan bounds and SoCConfig lookup.
    simrv::memory::RamView empty_ram{};
    TEST_CHECK(empty_ram.span().empty());
    TEST_CHECK(empty_ram.subspan(simrv::xlen::PhysAddr{0}, 4).empty());

    std::array<simrv::xlen::Byte, 64> ram_backing{};
    simrv::memory::RamView ram_view(ram_backing, simrv::xlen::PhysAddr{0x80000000});
    TEST_CHECK(ram_view.span().size() == 64);
    TEST_CHECK(ram_view.subspan(simrv::xlen::PhysAddr{0x80000010}, 16).size() == 16);
    TEST_CHECK(ram_view.subspan(simrv::xlen::PhysAddr{0x80000038}, 16).empty());
    TEST_CHECK(
        ram_view.subspan(simrv::xlen::PhysAddr{std::numeric_limits<simrv::xlen::Address>::max()}, 8)
            .empty());

    simrv::core::SoCConfig empty_soc{};
    TEST_CHECK(empty_soc.find(simrv::core::SoCDeviceKind::Uart) == nullptr);

    // 3. PciDevice config-space bounds, BAR alignment, MSI cap validation, and 64-bit ECAM split.
    struct TestPciDevice final : PciDevice {
        TestPciDevice()
            : PciDevice(PciVendorId{0x1234}, PciDeviceId{0x5678}, PciRevisionId{1},
                        PciClassCode{0x010000}) {
            init_bar(0, 0x1000, false, false);
        }
    };
    auto pci_dev = std::make_shared<TestPciDevice>();
    TEST_CHECK(pci_dev->config_read(PciConfigOffset{0x00}, 0) == 0xFFFFFFFFU);
    TEST_CHECK(pci_dev->config_read(PciConfigOffset{0x00}, 3) == 0xFFFFFFFFU);
    TEST_CHECK(pci_dev->config_read(PciConfigOffset{0x00}, 8) == 0xFFFFFFFFU);
    TEST_CHECK(pci_dev->config_read(PciConfigOffset{0xFF}, 4) == 0xFFFFFFFFU);
    pci_dev->config_write(PciConfigOffset{0xFF}, 0xAABBCCDDU, 4);

    pci_dev->config_write(PciConfigOffset{0x10}, 0x50001000U, 4);
    TEST_CHECK(pci_dev->bar_base(0) == simrv::xlen::PhysAddr{0x50001000});
    pci_dev->config_write(PciConfigOffset{0x10}, 0x1234U, 2);
    pci_dev->config_write(PciConfigOffset{0x11}, 0x20000000U, 4);
    TEST_CHECK(pci_dev->bar_base(0) == simrv::xlen::PhysAddr{0x50001000});

    pci_dev->init_msi(PciConfigOffset{0xFC}, true);
    TEST_CHECK(!pci_dev->is_msi_enabled());
    TEST_CHECK(pci_dev->msi_address() == simrv::xlen::PhysAddr{0});
    TEST_CHECK(pci_dev->msi_data() == 0);

    PcieRootComplex rc(nullptr);
    TEST_CHECK(
        rc.attach_device(PciBusNumber{0}, PciDeviceNumber{1}, PciFunctionNumber{0}, pci_dev));
    simrv::memory::TlChannelA ecam_req{};
    simrv::memory::TlChannelD ecam_resp{};
    ecam_req.opcode = simrv::memory::TlOpcodeA::Get;
    ecam_req.size = 3;  // 8-byte access split into two 32-bit ECAM reads
    ecam_req.address = simrv::xlen::PhysAddr{rc.ecam_node().base_address().raw() + (1U << 15)};
    TEST_CHECK(rc.ecam_node().handle_request(ecam_req, ecam_resp) && !ecam_resp.denied);
    TEST_CHECK((ecam_resp.data & 0xFFFFFFFFULL) == ((0x5678ULL << 16U) | 0x1234ULL));

    ecam_req.opcode = simrv::memory::TlOpcodeA::PutFullData;
    ecam_req.size = 3;
    ecam_req.address =
        simrv::xlen::PhysAddr{rc.ecam_node().base_address().raw() + (1U << 15) + 0x10};
    ecam_req.data = 0x0000000060002000ULL;
    TEST_CHECK(rc.ecam_node().handle_request(ecam_req, ecam_resp) && !ecam_resp.denied);
    TEST_CHECK(pci_dev->bar_base(0) == simrv::xlen::PhysAddr{0x60002000});

    ecam_req.opcode = simrv::memory::TlOpcodeA::Get;
    ecam_req.address = simrv::xlen::PhysAddr{std::numeric_limits<simrv::xlen::Address>::max()};
    TEST_CHECK(rc.ecam_node().handle_request(ecam_req, ecam_resp) && !ecam_resp.denied &&
               ecam_resp.data == 0xFFFFFFFFFFFFFFFFULL);

    // 4. HostAudioSink unaligned sample scaling and oversized buffer eviction bounds.
    auto& audio = HostAudioSink::instance();
    const float prev_vol = audio.volume();
    const bool prev_muted = audio.is_muted();
    audio.set_volume(0.5F);
    audio.set_muted(false);
    std::array<uint8_t, 5> odd_pcm{0x01, 0x02, 0x03, 0x04, 0x7F};
    audio.write_samples(odd_pcm.data(), odd_pcm.size());
    std::vector<uint8_t> huge_pcm(200000, 0x10);
    audio.write_samples(huge_pcm.data(), huge_pcm.size());
    audio.set_muted(true);
    audio.write_samples(huge_pcm.data(), huge_pcm.size());
    audio.set_volume(prev_vol);
    audio.set_muted(prev_muted);

    // 5. VirtioRngQueue chunked DMA bounds and wraparound rejection.
    std::array<uint8_t, 4096> vq_ram{};
    auto vq_read = [&](uint64_t addr, void* dst, std::size_t len) -> bool {
        if (addr > vq_ram.size() || len > vq_ram.size() - static_cast<std::size_t>(addr)) {
            return false;
        }
        std::memcpy(dst, vq_ram.data() + addr, len);
        return true;
    };
    auto vq_write = [&](uint64_t addr, const void* src, std::size_t len) -> bool {
        if (addr > vq_ram.size() || len > vq_ram.size() - static_cast<std::size_t>(addr)) {
            return false;
        }
        std::memcpy(vq_ram.data() + addr, src, len);
        return true;
    };
    QueueState rng_q{};
    rng_q.num_max = 4;
    rng_q.num = 4;
    rng_q.ready = 1;
    rng_q.desc_addr = 0x100;
    rng_q.driver_addr = 0x200;
    rng_q.device_addr = 0x300;
    rng_q.last_avail_idx = 0;

    VirtqDesc rng_desc0{.addr = 0x400, .len = 0x100000U, .flags = kVirtqDescFWrite, .next = 0};
    VirtqDesc rng_desc1{.addr = std::numeric_limits<uint64_t>::max() - 8ULL,
                        .len = 64,
                        .flags = kVirtqDescFWrite,
                        .next = 0};
    std::memcpy(vq_ram.data() + rng_q.desc_addr, &rng_desc0, sizeof(rng_desc0));
    std::memcpy(vq_ram.data() + rng_q.desc_addr + sizeof(VirtqDesc), &rng_desc1, sizeof(rng_desc1));
    uint16_t avail_idx = 2;
    uint16_t ring0 = 0;
    uint16_t ring1 = 1;
    std::memcpy(vq_ram.data() + rng_q.driver_addr + 2, &avail_idx, sizeof(avail_idx));
    std::memcpy(vq_ram.data() + rng_q.driver_addr + 4, &ring0, sizeof(ring0));
    std::memcpy(vq_ram.data() + rng_q.driver_addr + 6, &ring1, sizeof(ring1));

    RngBackend rng_backend;
    int rng_irq_count = 0;
    process_rng_queue(rng_q, rng_backend, vq_read, vq_write, [&]() { ++rng_irq_count; });
    TEST_CHECK(rng_irq_count == 1);
    VirtqUsedElem used0{};
    VirtqUsedElem used1{};
    std::memcpy(&used0, vq_ram.data() + rng_q.device_addr + 4, sizeof(used0));
    std::memcpy(&used1, vq_ram.data() + rng_q.device_addr + 4 + sizeof(VirtqUsedElem),
                sizeof(used1));
    TEST_CHECK(used0.len == 3072);
    TEST_CHECK(used1.len == 0);

    // 6. VirtioSoundBackend controlq & txq DMA read failure handling.
    std::array<uint8_t, 4096> snd_ram{};
    auto snd_dma_read = [&](simrv::xlen::PhysAddr addr, void* dst, std::size_t len) -> bool {
        const auto raw = addr.raw();
        if (raw > snd_ram.size() || len > snd_ram.size() - static_cast<std::size_t>(raw)) {
            return false;
        }
        std::memcpy(dst, snd_ram.data() + raw, len);
        return true;
    };
    auto snd_dma_write = [&](simrv::xlen::PhysAddr addr, const void* src, std::size_t len) -> bool {
        const auto raw = addr.raw();
        if (raw > snd_ram.size() || len > snd_ram.size() - static_cast<std::size_t>(raw)) {
            return false;
        }
        std::memcpy(snd_ram.data() + raw, src, len);
        return true;
    };
    VirtioSoundBackend sound_backend;
    QueueState ctl_q{};
    ctl_q.num_max = 4;
    ctl_q.num = 4;
    ctl_q.ready = 1;
    ctl_q.desc_addr = 0x100;
    ctl_q.driver_addr = 0x200;
    ctl_q.device_addr = 0x300;
    VirtioSndPcmSetParams ctl_params{};
    ctl_params.hdr.hdr.code = kVirtioSndRPcmSetParams;
    std::memcpy(snd_ram.data() + 0xFFC, &ctl_params.hdr.hdr, sizeof(ctl_params.hdr.hdr));
    VirtqDesc ctl_desc0{
        .addr = 0xFFC, .len = sizeof(VirtioSndPcmSetParams), .flags = kVirtqDescFNext, .next = 1};
    VirtqDesc ctl_desc1{
        .addr = 0x500, .len = sizeof(VirtioSndHdr), .flags = kVirtqDescFWrite, .next = 0};
    std::memcpy(snd_ram.data() + ctl_q.desc_addr, &ctl_desc0, sizeof(ctl_desc0));
    std::memcpy(snd_ram.data() + ctl_q.desc_addr + sizeof(VirtqDesc), &ctl_desc1,
                sizeof(ctl_desc1));
    uint16_t ctl_avail_idx = 1;
    uint16_t ctl_ring0 = 0;
    std::memcpy(snd_ram.data() + ctl_q.driver_addr + 2, &ctl_avail_idx, sizeof(ctl_avail_idx));
    std::memcpy(snd_ram.data() + ctl_q.driver_addr + 4, &ctl_ring0, sizeof(ctl_ring0));
    int snd_irqs = 0;
    sound_backend.process_controlq(ctl_q, snd_dma_read, snd_dma_write, [&]() { ++snd_irqs; });
    TEST_CHECK(snd_irqs == 1);
    VirtioSndHdr ctl_resp{};
    std::memcpy(&ctl_resp, snd_ram.data() + 0x500, sizeof(ctl_resp));
    TEST_CHECK(ctl_resp.code == kVirtioSndSNotSupp);

    const uint64_t bytes_before_tx = audio.total_bytes();
    QueueState tx_q{};
    tx_q.num_max = 4;
    tx_q.num = 4;
    tx_q.ready = 1;
    tx_q.desc_addr = 0x600;
    tx_q.driver_addr = 0x700;
    tx_q.device_addr = 0x800;
    VirtqDesc tx_desc0{
        .addr = 0x900, .len = sizeof(VirtioSndPcmXfer), .flags = kVirtqDescFNext, .next = 1};
    VirtqDesc tx_desc1{.addr = 0xA00, .len = 250000U, .flags = kVirtqDescFNext, .next = 2};
    VirtqDesc tx_desc2{
        .addr = 0xB00, .len = sizeof(VirtioSndPcmStatus), .flags = kVirtqDescFWrite, .next = 0};
    std::memcpy(snd_ram.data() + tx_q.desc_addr, &tx_desc0, sizeof(tx_desc0));
    std::memcpy(snd_ram.data() + tx_q.desc_addr + sizeof(VirtqDesc), &tx_desc1, sizeof(tx_desc1));
    std::memcpy(snd_ram.data() + tx_q.desc_addr + 2 * sizeof(VirtqDesc), &tx_desc2,
                sizeof(tx_desc2));
    uint16_t tx_avail_idx = 1;
    uint16_t tx_ring0 = 0;
    std::memcpy(snd_ram.data() + tx_q.driver_addr + 2, &tx_avail_idx, sizeof(tx_avail_idx));
    std::memcpy(snd_ram.data() + tx_q.driver_addr + 4, &tx_ring0, sizeof(tx_ring0));
    sound_backend.process_txq(tx_q, snd_dma_read, snd_dma_write, [&]() { ++snd_irqs; });
    TEST_CHECK(snd_irqs == 2);
    TEST_CHECK(audio.total_bytes() == bytes_before_tx);

    // 7. SymbolTable ELF section caps and non-NUL string table hardening.
    const fs::path tmp_elf_dir = fs::temp_directory_path() / "simrv_memory_safety_elf_tests";
    fs::create_directories(tmp_elf_dir);
    struct TempDirCleanup {
        fs::path path;
        ~TempDirCleanup() {
            std::error_code ec;
            fs::remove_all(path, ec);
        }
    } cleanup{tmp_elf_dir};

    auto write_file_bytes = [](const fs::path& p, const std::vector<uint8_t>& bytes) {
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    };

    auto make_elf64_header = [](uint16_t shnum, uint64_t shoff) {
        simrv::util::elf::Elf64_Ehdr ehdr{};
        ehdr.e_ident[0] = 0x7f;
        ehdr.e_ident[1] = 'E';
        ehdr.e_ident[2] = 'L';
        ehdr.e_ident[3] = 'F';
        ehdr.e_ident[4] = simrv::util::elf::kClass64;
        ehdr.e_shnum = shnum;
        ehdr.e_shoff = shoff;
        return ehdr;
    };

    {
        const auto ehdr = make_elf64_header(60000, sizeof(simrv::util::elf::Elf64_Ehdr));
        std::vector<uint8_t> bytes(sizeof(ehdr));
        std::memcpy(bytes.data(), &ehdr, sizeof(ehdr));
        const fs::path p = tmp_elf_dir / "excess_sections.elf";
        write_file_bytes(p, bytes);
        simrv::debug::SymbolTable sym;
        TEST_CHECK(!sym.load_from_elf(p.string()));
    }
    {
        const auto ehdr = make_elf64_header(1, sizeof(simrv::util::elf::Elf64_Ehdr));
        simrv::util::elf::Elf64_Shdr sym_shdr{};
        sym_shdr.sh_type = simrv::util::elf::kSymbolTable;
        sym_shdr.sh_link = 99;
        std::vector<uint8_t> bytes(sizeof(ehdr) + sizeof(sym_shdr));
        std::memcpy(bytes.data(), &ehdr, sizeof(ehdr));
        std::memcpy(bytes.data() + sizeof(ehdr), &sym_shdr, sizeof(sym_shdr));
        const fs::path p = tmp_elf_dir / "bad_link.elf";
        write_file_bytes(p, bytes);
        simrv::debug::SymbolTable sym;
        TEST_CHECK(!sym.load_from_elf(p.string()));
    }
    {
        const uint64_t shoff = sizeof(simrv::util::elf::Elf64_Ehdr);
        const uint64_t symoff = shoff + 2 * sizeof(simrv::util::elf::Elf64_Shdr);
        const uint64_t stroff = symoff + sizeof(simrv::util::elf::Elf64_Sym);
        const auto ehdr = make_elf64_header(2, shoff);

        simrv::util::elf::Elf64_Shdr shdr0{};
        shdr0.sh_type = simrv::util::elf::kSymbolTable;
        shdr0.sh_offset = symoff;
        shdr0.sh_size = sizeof(simrv::util::elf::Elf64_Sym);
        shdr0.sh_link = 1;

        simrv::util::elf::Elf64_Shdr shdr1{};
        shdr1.sh_offset = stroff;
        shdr1.sh_size = 4;

        simrv::util::elf::Elf64_Sym sym_rec{};
        sym_rec.st_name = 1;
        sym_rec.st_info = simrv::util::elf::kSymbolFunction;
        sym_rec.st_value = 0x80001000ULL;

        std::array<char, 4> unterminated{'\0', 'f', 'o', 'o'};
        std::vector<uint8_t> bytes(stroff + unterminated.size());
        std::memcpy(bytes.data(), &ehdr, sizeof(ehdr));
        std::memcpy(bytes.data() + shoff, &shdr0, sizeof(shdr0));
        std::memcpy(bytes.data() + shoff + sizeof(shdr0), &shdr1, sizeof(shdr1));
        std::memcpy(bytes.data() + symoff, &sym_rec, sizeof(sym_rec));
        std::memcpy(bytes.data() + stroff, unterminated.data(), unterminated.size());

        const fs::path p = tmp_elf_dir / "unterminated_strtab.elf";
        write_file_bytes(p, bytes);
        simrv::debug::SymbolTable sym;
        TEST_CHECK(sym.load_from_elf(p.string()));
        const auto found = sym.lookup_name("fo");
        TEST_CHECK(found.has_value() && *found == simrv::xlen::VirtAddr{0x80001000});
    }

    // 8. PFlash reset() controller state restoration and wraparound-safe MMIO bounds.
    simrv::core::MachineConfig pflash_cfg{};
    pflash_cfg.memory.dram_size = 0x100000;
    simrv::core::Machine pflash_machine(pflash_cfg);
    PFlash pflash(pflash_machine, simrv::xlen::PhysAddr{0x20000000}, 0x10000);
    pflash.write8(simrv::memory::MmioOffset{0}, 0x40);
    pflash.reset();
    TEST_CHECK(pflash.read8(simrv::memory::MmioOffset{0}) == 0xFF);

    constexpr auto kMaxOffset = std::numeric_limits<simrv::xlen::Address>::max();
    TEST_CHECK(pflash.read16(simrv::memory::MmioOffset{kMaxOffset}) == 0);
    TEST_CHECK(pflash.read32(simrv::memory::MmioOffset{kMaxOffset}) == 0);
    TEST_CHECK(pflash.read64(simrv::memory::MmioOffset{kMaxOffset}) == 0);
    pflash.write8(simrv::memory::MmioOffset{0}, 0x40);
    pflash.write16(simrv::memory::MmioOffset{kMaxOffset}, 0x1234);
    pflash.write8(simrv::memory::MmioOffset{0}, 0x40);
    pflash.write32(simrv::memory::MmioOffset{kMaxOffset}, 0x12345678U);
    pflash.write8(simrv::memory::MmioOffset{0}, 0x40);
    pflash.write64(simrv::memory::MmioOffset{kMaxOffset}, 0x123456789ABCDEF0ULL);
    pflash.reset();
    TEST_CHECK(!pflash.load_image(tmp_elf_dir / "missing_pflash.bin"));
}

int main() {
    test_plugin_host_services_are_per_device();
    test_serial_plugin_replaces_builtin();
    test_serial_plugin_device_ticks_in_fast_baremetal();
    test_mmio_plugin_replaces_standalone_builtin();
    test_framebuffer_uses_mmio_device_context();
#ifdef TEST_RV98_KEYBOARD_PLUGIN_PATH
    test_external_input_plugin_translates_key_events();
#endif
    test_replacement_creation_failure_is_not_silently_ignored();
    test_capability_registration_is_atomic();
    test_v2_host_services_dma_and_logging();
    test_v2_cycle_ticking();
    test_v1_compatibility();
    test_scaffolding_and_formalized_configs();
    test_external_mmio_bounds_and_dma_overflow();
    test_ring_buffer_ethernet_subword_and_net_bounds();
    test_ring_buffer_ethernet_dhcp_round_trip();
    test_machine_reinitialize_and_reset_cleans_up_devices();
    test_memory_safety_audit_and_cxx_profiles();
#ifdef TEST_TIMER_PLUGIN_PATH
    test_example_timer_plugin_integration();
    test_timer_plugin_advances_in_fast_baremetal();
#endif
#ifdef TEST_DMA_PLUGIN_PATH
    test_example_dma_accel_plugin_integration();
#endif
    std::cout << "Device plugin tests passed\n";
}
