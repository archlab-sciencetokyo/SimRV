/** Optional serial capability for a version-1 external MMIO device plugin. */
#ifndef SIMRV_DEVICE_EXTERNAL_SERIAL_DEVICE_H
#define SIMRV_DEVICE_EXTERNAL_SERIAL_DEVICE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define SIMRV_EXTERNAL_SERIAL_API_VERSION 1

typedef struct SimRvSerialHostServices {
    void* host_ctx;
    uint32_t irq_number;
    /* Called on the simulation thread to write a byte to the active host console. */
    void (*write_byte)(void* host_ctx, uint8_t byte);
} SimRvSerialHostServices;

typedef struct SimRvExternalSerialDevice {
    uint32_t api_version;
    /* The host retains this service table until after the MMIO destroy callback. */
    void (*set_serial_host_services)(void* ctx, const SimRvSerialHostServices* services);
    /* May be called by an input thread concurrently with MMIO and cycle callbacks. */
    void (*receive_byte)(void* ctx, uint8_t byte);
    /* Called on the simulation thread; publish pending interrupt changes here. */
    void (*advance_cycles)(void* ctx, uint64_t cycles);
    void (*service_interrupts)(void* ctx);
    int (*has_active_transfer)(void* ctx);
    int (*is_interrupt_pending)(void* ctx);
} SimRvExternalSerialDevice;

typedef const SimRvExternalSerialDevice* (*SimRvSerialPluginInitFn)(void);
/* Optional exported entry point: simrv_serial_plugin_init. */
#ifdef __cplusplus
}
#endif
#endif
