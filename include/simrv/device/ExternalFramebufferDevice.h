/** Optional framebuffer capability for a version-1 external MMIO device plugin. */
#ifndef SIMRV_DEVICE_EXTERNAL_FRAMEBUFFER_DEVICE_H
#define SIMRV_DEVICE_EXTERNAL_FRAMEBUFFER_DEVICE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SIMRV_EXTERNAL_FRAMEBUFFER_API_VERSION 1

typedef enum SimRvFramebufferFormat {
    SIMRV_FB_FORMAT_A8R8G8B8 = 0,
    SIMRV_FB_FORMAT_R5G6B5 = 1,
} SimRvFramebufferFormat;

typedef struct SimRvFramebufferInfo {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t bpp; /* Bytes per pixel: 4 for A8R8G8B8, 2 for R5G6B5. */
    SimRvFramebufferFormat format;
} SimRvFramebufferInfo;

typedef struct SimRvExternalFramebufferDevice {
    uint32_t api_version;
    /* Return 0 on success, populating out_info. */
    int (*get_info)(void* ctx, SimRvFramebufferInfo* out_info);
    /* Return pointer to contiguous pixel buffer and set out_size in bytes. */
    const void* (*get_buffer)(void* ctx, size_t* out_size);
    /* Monotonically advancing counter or hash used for selective TUI refresh. */
    uint64_t (*get_frame_id)(void* ctx);
} SimRvExternalFramebufferDevice;

typedef const SimRvExternalFramebufferDevice* (*SimRvFramebufferPluginInitFn)(void);
/* Optional exported entry point: simrv_framebuffer_plugin_init. */

#ifdef __cplusplus
}
#endif

#endif  // SIMRV_DEVICE_EXTERNAL_FRAMEBUFFER_DEVICE_H
