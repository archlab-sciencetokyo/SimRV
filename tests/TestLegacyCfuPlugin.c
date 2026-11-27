/* Original v1 layout, intentionally independent of the current public header. */
#include <stdint.h>

struct LegacyCfu {
    uint32_t api_version;
    uint32_t (*execute)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t*);
    void (*reset)(void);
};

static uint32_t execute(uint32_t f7, uint32_t f3, uint32_t a, uint32_t b, uint32_t* latency) {
    *latency = 3;
    return a ^ b ^ f7 ^ f3;
}

static const struct LegacyCfu plugin = {1, execute, 0};

const struct LegacyCfu* simrv_cfu_init(void) { return &plugin; }
