/* Original v1 layouts, intentionally independent of the current public header. */
#include <stdint.h>

struct LegacyHostServices {
    void* host_ctx;
    void (*set_irq)(void*, uint32_t, int);
    void (*request_reboot)(void*);
    void (*request_poweroff)(void*);
};

struct LegacyDevice {
    uint32_t api_version;
    const char* name;
    const char* compatible;
    uint64_t default_base;
    uint64_t default_size;
    int (*init)(void**, const char*);
    void (*reset)(void*);
    int (*read)(void*, uint64_t, uint32_t, uint64_t*);
    int (*write)(void*, uint64_t, uint32_t, uint64_t);
    void (*destroy)(void*);
    void (*set_host_services)(void*, const struct LegacyHostServices*);
};

static const struct LegacyHostServices* host;

static int read_register(void* ctx, uint64_t offset, uint32_t size, uint64_t* value) {
    (void)ctx;
    (void)size;
    *value = offset == 8 ? (host != 0 && host->host_ctx != 0 && host->set_irq != 0)
                         : (0x12340000u | offset);
    return 0;
}

static int write_register(void* ctx, uint64_t offset, uint32_t size, uint64_t value) {
    (void)ctx;
    (void)offset;
    (void)size;
    if (host != 0 && host->set_irq != 0) host->set_irq(host->host_ctx, 7, value != 0);
    return 0;
}

static void set_host(void* ctx, const struct LegacyHostServices* services) {
    (void)ctx;
    host = services;
}

static const struct LegacyDevice plugin = {
    1, "legacy-device", "test,legacy-device", 0x20000000, 0x1000,
    0, 0, read_register, write_register, 0, set_host,
};

const struct LegacyDevice* simrv_device_plugin_init(void) { return &plugin; }
