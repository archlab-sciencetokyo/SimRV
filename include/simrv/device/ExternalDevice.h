/**
 * @file ExternalDevice.h
 * @brief Public C-ABI interface for external/plugin MMIO device simulation in SimRV.
 */
#ifndef SIMRV_DEVICE_EXTERNAL_DEVICE_H
#define SIMRV_DEVICE_EXTERNAL_DEVICE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SIMRV_EXTERNAL_DEVICE_API_VERSION 1

/**
 * @brief Host simulation services exposed to external device plugins.
 */
typedef struct SimRvHostServices {
    void* host_ctx;
    void (*set_irq)(void* host_ctx, uint32_t irq, int level);
    void (*request_reboot)(void* host_ctx);
    void (*request_poweroff)(void* host_ctx);
} SimRvHostServices;

/**
 * @brief Descriptor for an external MMIO device simulated outside the core engine.
 */
typedef struct SimRvExternalDevice {
    uint32_t api_version;   /**< Must be SIMRV_EXTERNAL_DEVICE_API_VERSION (1) */
    const char* name;       /**< Human-readable device name (e.g. "rvcomp-mmc") */
    const char* compatible; /**< DeviceTree compatible string (e.g. "isct,rvcomp-mmc") */
    uint64_t default_base;  /**< Default MMIO base physical address */
    uint64_t default_size;  /**< MMIO address space size in bytes */

    /**
     * @brief Initialize device instance.
     * @param ctx Pointer to output user context pointer.
     * @param args Optional configuration arguments or backing file path.
     * @return 0 on success, non-zero on error.
     */
    int (*init)(void** ctx, const char* args);

    /**
     * @brief Hardware reset callback.
     * @param ctx User context pointer.
     */
    void (*reset)(void* ctx);

    /**
     * @brief MMIO Read callback.
     * @param ctx User context pointer.
     * @param offset Relative byte offset from base address.
     * @param size Access width in bytes (1, 2, 4, or 8).
     * @param out_val Pointer to destination word.
     * @return 0 on success, non-zero on bus error.
     */
    int (*read)(void* ctx, uint64_t offset, uint32_t size, uint64_t* out_val);

    /**
     * @brief MMIO Write callback.
     * @param ctx User context pointer.
     * @param offset Relative byte offset from base address.
     * @param size Access width in bytes (1, 2, 4, or 8).
     * @param val Data to write.
     * @return 0 on success, non-zero on bus error.
     */
    int (*write)(void* ctx, uint64_t offset, uint32_t size, uint64_t val);

    /**
     * @brief Destroy device instance and free user context.
     * @param ctx User context pointer.
     */
    void (*destroy)(void* ctx);

    /**
     * @brief Optional callback to receive host services (IRQs, power/reset).
     * @param ctx User context pointer.
     * @param services Host services structure.
     */
    void (*set_host_services)(void* ctx, const SimRvHostServices* services);
} SimRvExternalDevice;

/**
 * @brief Standard plugin entry point exported by external device shared libraries (.so).
 */
typedef const SimRvExternalDevice* (*SimRvDevicePluginInitFn)(void);

#ifdef __cplusplus
}
#endif

#endif /* SIMRV_DEVICE_EXTERNAL_DEVICE_H */
