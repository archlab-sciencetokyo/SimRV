/**
 * @file ExternalInputDevice.h
 * @brief Optional input device capability for external device plugins.
 */
#ifndef SIMRV_DEVICE_EXTERNAL_INPUT_DEVICE_H
#define SIMRV_DEVICE_EXTERNAL_INPUT_DEVICE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SIMRV_EXTERNAL_INPUT_API_VERSION 1

typedef struct SimRvExternalInputDevice {
    uint32_t api_version;
    /* Called on input events (Linux keycode, pressed != 0 for press, 0 for release). */
    void (*send_key)(void* ctx, uint16_t keycode, int pressed);
} SimRvExternalInputDevice;

typedef const SimRvExternalInputDevice* (*SimRvInputPluginInitFn)(void);
/* Optional exported entry point: simrv_input_plugin_init. */

#ifdef __cplusplus
}
#endif

#endif  // SIMRV_DEVICE_EXTERNAL_INPUT_DEVICE_H
