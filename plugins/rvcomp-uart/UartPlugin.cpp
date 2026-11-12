/** RVComp UART register interface, supplied as a serial device plugin. */
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <new>

#include "simrv/device/ExternalDevice.h"
#include "simrv/device/ExternalSerialDevice.h"

namespace {
constexpr size_t kRxCapacity = 2048;
struct Uart {
    std::mutex mutex;
    std::deque<uint8_t> rx;
    std::atomic<bool> irq_dirty{false};
    const SimRvHostServices* host = nullptr;
    const SimRvSerialHostServices* serial = nullptr;
};

int init(void** out, const char*) {
    *out = new (std::nothrow) Uart;
    return *out ? 0 : -1;
}
void destroy(void* ctx) { delete static_cast<Uart*>(ctx); }
void reset(void* ctx) {
    auto& uart = *static_cast<Uart*>(ctx);
    {
        std::scoped_lock lock(uart.mutex);
        uart.rx.clear();
    }
    uart.irq_dirty.store(true, std::memory_order_release);
}
int read(void* ctx, uint64_t offset, uint32_t size, uint64_t* out) {
    if (!out || (size != 1 && size != 2 && size != 4)) return -1;
    auto& uart = *static_cast<Uart*>(ctx);
    std::scoped_lock lock(uart.mutex);
    switch (offset) {
        case 0:
            *out = uart.rx.empty() ? 0 : uart.rx.front();
            if (!uart.rx.empty()) uart.rx.pop_front();
            uart.irq_dirty.store(true, std::memory_order_release);
            return 0;
        case 4:
            *out = 0;
            return 0;  // Transmit is delivered to the host immediately.
        case 8:
            *out = uart.rx.empty() ? 1 : 0;
            return 0;
        default:
            return -1;
    }
}
int write(void* ctx, uint64_t offset, uint32_t size, uint64_t value) {
    auto& uart = *static_cast<Uart*>(ctx);
    if (offset != 0 || (size != 1 && size != 2 && size != 4) || !uart.serial ||
        !uart.serial->write_byte) {
        return -1;
    }
    uart.serial->write_byte(uart.serial->host_ctx, static_cast<uint8_t>(value));
    return 0;
}
void host_services(void* ctx, const SimRvHostServices* services) {
    static_cast<Uart*>(ctx)->host = services;
}
void serial_services(void* ctx, const SimRvSerialHostServices* services) {
    static_cast<Uart*>(ctx)->serial = services;
}
void receive(void* ctx, uint8_t byte) {
    auto& uart = *static_cast<Uart*>(ctx);
    {
        std::scoped_lock lock(uart.mutex);
        if (uart.rx.size() >= kRxCapacity) return;
        uart.rx.push_back(byte);
    }
    uart.irq_dirty.store(true, std::memory_order_release);
}
int pending(void* ctx) {
    auto& uart = *static_cast<Uart*>(ctx);
    std::scoped_lock lock(uart.mutex);
    return !uart.rx.empty();
}
void service(void* ctx) {
    auto& uart = *static_cast<Uart*>(ctx);
    if (!uart.irq_dirty.exchange(false, std::memory_order_acq_rel)) return;
    if (uart.host && uart.host->set_irq && uart.serial)
        uart.host->set_irq(uart.host->host_ctx, uart.serial->irq_number, pending(ctx));
}
void advance(void* ctx, uint64_t) { service(ctx); }
const SimRvExternalDevice device{
    .api_version = SIMRV_EXTERNAL_DEVICE_API_VERSION,
    .name = "rvcomp-uart",
    .compatible = "isct,rvcomp-uart",
    .default_base = 0x10000000,
    .default_size = 0x10,
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
    .advance_cycles = advance,
    .service_interrupts = service,
    .has_active_transfer = nullptr,
    .is_interrupt_pending = pending,
};
}  // namespace
extern "C" const SimRvExternalDevice* simrv_device_plugin_init() { return &device; }
extern "C" const SimRvExternalSerialDevice* simrv_serial_plugin_init() { return &serial; }
