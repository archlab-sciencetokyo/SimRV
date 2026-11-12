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

#define SIMRV_EXTERNAL_DEVICE_API_VERSION_V1 1
#define SIMRV_EXTERNAL_DEVICE_API_VERSION 2

/**
 * @brief Severity levels for host log messages.
 */
typedef enum SimRvLogLevel {
    SIMRV_LOG_TRACE = 0,
    SIMRV_LOG_DEBUG = 1,
    SIMRV_LOG_INFO = 2,
    SIMRV_LOG_WARN = 3,
    SIMRV_LOG_ERROR = 4,
} SimRvLogLevel;

/**
 * @brief Host simulation services exposed to external device plugins.
 */
typedef struct SimRvHostServices {
    void* host_ctx;
    void (*set_irq)(void* host_ctx, uint32_t irq, int level);
    void (*request_reboot)(void* host_ctx);
    void (*request_poweroff)(void* host_ctx);

    /* V2 Host Services */
    /**
     * @brief Direct Physical Memory Read (DMA Master).
     * @param host_ctx Host context pointer.
     * @param paddr Guest physical address.
     * @param dst Destination buffer to receive copied bytes.
     * @param len Number of bytes to read.
     * @return 0 on success, non-zero on bus/address error.
     */
    int (*dma_read)(void* host_ctx, uint64_t paddr, void* dst, size_t len);

    /**
     * @brief Direct Physical Memory Write (DMA Master).
     * @param host_ctx Host context pointer.
     * @param paddr Guest physical address.
     * @param src Source buffer containing bytes to write.
     * @param len Number of bytes to write.
     * @return 0 on success, non-zero on bus/address error.
     */
    int (*dma_write)(void* host_ctx, uint64_t paddr, const void* src, size_t len);

    /**
     * @brief Query current simulation cycle count.
     * @param host_ctx Host context pointer.
     * @return Current simulation cycle count.
     */
    uint64_t (*get_cycle_count)(void* host_ctx);

    /**
     * @brief Emit structured log message through SimRV's logger.
     * @param host_ctx Host context pointer.
     * @param level Severity level (SimRvLogLevel).
     * @param msg Null-terminated string message.
     */
    void (*log_msg)(void* host_ctx, int level, const char* msg);
} SimRvHostServices;

/**
 * @brief Descriptor for an external MMIO device simulated outside the core engine.
 */
typedef struct SimRvExternalDevice {
    uint32_t api_version;   /**< Must be SIMRV_EXTERNAL_DEVICE_API_VERSION (2) or (1) */
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

    /* V2 Callbacks (optional in v2, may be NULL) */
    /**
     * @brief Simulation cycle tick callback.
     * Called on the simulation thread during cycle advancement.
     * @param ctx User context pointer.
     * @param cycles Number of clock cycles advanced.
     */
    void (*advance_cycles)(void* ctx, uint64_t cycles);
} SimRvExternalDevice;

/**
 * @brief Standard plugin entry point exported by external device shared libraries (.so).
 */
typedef const SimRvExternalDevice* (*SimRvDevicePluginInitFn)(void);

#ifdef __cplusplus
}
#endif

#endif /* SIMRV_DEVICE_EXTERNAL_DEVICE_H */
