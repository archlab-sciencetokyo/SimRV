#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/MachineConfig.hpp"
#include "simrv/core/TelemetrySink.hpp"
#include "simrv/device/ExternalDevicePlugin.hpp"
#include "simrv/device/ExternalFramebufferDevice.h"
#include "simrv/device/FramebufferDevice.hpp"
#include "simrv/device/ExternalSerialDevice.h"
#include "simrv/device/SerialDevice.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/TileLinkBus.hpp"

static_assert(std::is_standard_layout_v<SimRvSerialHostServices>);
static_assert(std::is_standard_layout_v<SimRvExternalSerialDevice>);
static_assert(SIMRV_EXTERNAL_SERIAL_API_VERSION == 1);
static_assert(std::is_standard_layout_v<SimRvExternalFramebufferDevice>);

#define TEST_CHECK(condition)                           \
    do {                                                \
        if (!(condition)) {                             \
            std::cerr << "Failed: " #condition << "\n"; \
            std::abort();                               \
        }                                               \
    } while (false)

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
    config.base = 0x10010000;
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

int main() {
    test_plugin_host_services_are_per_device();
    test_serial_plugin_replaces_builtin();
    test_mmio_plugin_replaces_standalone_builtin();
    test_framebuffer_uses_mmio_device_context();
    test_replacement_creation_failure_is_not_silently_ignored();
    test_capability_registration_is_atomic();
    std::cout << "Device plugin tests passed\n";
}
