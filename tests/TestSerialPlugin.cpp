#include <atomic>
#include <cstdint>

#include "simrv/device/ExternalDevice.h"
#include "simrv/device/ExternalSerialDevice.h"

namespace {
struct Device {
    std::atomic<int> input{-1};
    const SimRvSerialHostServices* serial{};
    const SimRvHostServices* host{};
    bool services_ready_at_reset{};
};
int init(void** out, const char*) {
    *out = new Device;
    return 0;
}
void destroy(void* ctx) { delete static_cast<Device*>(ctx); }
void reset(void* ctx) {
    auto* dev = static_cast<Device*>(ctx);
    dev->services_ready_at_reset = dev->serial && dev->host;
}
int read(void* ctx, uint64_t offset, uint32_t, uint64_t* out) {
    auto* dev = static_cast<Device*>(ctx);
    if (offset == 8) return -1;
    if (offset == 12) {
        *out = 0x1234;
        return 0;
    }
    *out = offset == 0   ? static_cast<uint64_t>(dev->input.exchange(-1))
           : offset == 4 ? dev->services_ready_at_reset
                         : 0;
    return 0;
}
int write(void* ctx, uint64_t offset, uint32_t, uint64_t value) {
    auto* dev = static_cast<Device*>(ctx);
    if (offset == 0 && dev->serial) dev->serial->write_byte(dev->serial->host_ctx, value);
    return 0;
}
void serial_services(void* ctx, const SimRvSerialHostServices* services) {
    static_cast<Device*>(ctx)->serial = services;
}
void host_services(void* ctx, const SimRvHostServices* services) {
    static_cast<Device*>(ctx)->host = services;
}
void receive(void* ctx, uint8_t byte) { static_cast<Device*>(ctx)->input.store(byte); }
int pending(void* ctx) { return static_cast<Device*>(ctx)->input.load() >= 0; }
void service(void* ctx) {
    auto* dev = static_cast<Device*>(ctx);
    dev->host->set_irq(dev->host->host_ctx, dev->serial->irq_number, pending(ctx));
}
const SimRvExternalDevice device{
    .api_version = SIMRV_EXTERNAL_DEVICE_API_VERSION,
    .name = "test-serial",
    .compatible = "test,serial",
    .default_base = 0x10000000,
    .default_size = 0x100,
    .init = init,
    .reset = reset,
    .read = read,
    .write = write,
    .destroy = destroy,
    .set_host_services = host_services,
};
const SimRvExternalSerialDevice serial{
    .api_version = SIMRV_EXTERNAL_SERIAL_API_VERSION,
    .set_serial_host_services = serial_services,
    .receive_byte = receive,
    .advance_cycles = nullptr,
    .service_interrupts = service,
    .has_active_transfer = nullptr,
    .is_interrupt_pending = pending,
};
}  // namespace
extern "C" const SimRvExternalDevice* simrv_device_plugin_init() { return &device; }
extern "C" const SimRvExternalSerialDevice* simrv_serial_plugin_init() { return &serial; }
