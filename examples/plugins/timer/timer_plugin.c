/**
 * @file timer_plugin.c
 * @brief Example SimRV dynamic external device plugin: cycle-accurate timer peripheral.
 *
 * Demonstrates:
 * - Version 2 C ABI (SIMRV_EXTERNAL_DEVICE_API_VERSION = 2)
 * - Deterministic simulation cycle advancement via `advance_cycles`
 * - Platform IRQ assertion via `SimRvHostServices::set_irq`
 * - Structured simulator logging via `SimRvHostServices::log_msg`
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "simrv/device/ExternalDevice.h"

#define TIMER_REG_CONTROL 0x00U /* Bit 0: Enable, Bit 1: IRQ Enable */
#define TIMER_REG_STATUS 0x04U  /* Bit 0: Expired / Interrupt Pending (W1C) */
#define TIMER_REG_LIMIT 0x08U   /* Target cycle duration */
#define TIMER_REG_COUNTER 0x0CU /* Current elapsed cycles (RO) */

#define CTRL_ENABLE (1U << 0)
#define CTRL_INT_ENABLE (1U << 1)
#define STAT_EXPIRED (1U << 0)

typedef struct TimerState {
    const SimRvHostServices* services;
    uint32_t control;
    uint32_t status;
    uint32_t limit;
    uint32_t counter;
    uint32_t irq_num;
} TimerState;

static int timer_init(void** out_ctx, const char* args) {
    TimerState* st = (TimerState*)calloc(1, sizeof(TimerState));
    if (!st) return -1;
    st->irq_num = 14; /* Default platform IRQ line */
    if (args && *args != '\0') {
        int parsed_irq = atoi(args);
        if (parsed_irq > 0) st->irq_num = (uint32_t)parsed_irq;
    }
    *out_ctx = st;
    return 0;
}

static void timer_reset(void* ctx) {
    TimerState* st = (TimerState*)ctx;
    if (!st) return;
    st->control = 0;
    st->status = 0;
    st->limit = 0;
    st->counter = 0;
    if (st->services && st->services->set_irq) {
        st->services->set_irq(st->services->host_ctx, st->irq_num, 0);
    }
}

static int timer_read(void* ctx, uint64_t offset, uint32_t size, uint64_t* out_val) {
    TimerState* st = (TimerState*)ctx;
    if (!st || !out_val || size != 4) {
        if (out_val) *out_val = 0;
        return 0;
    }

    switch (offset) {
        case TIMER_REG_CONTROL:
            *out_val = st->control;
            break;
        case TIMER_REG_STATUS:
            *out_val = st->status;
            break;
        case TIMER_REG_LIMIT:
            *out_val = st->limit;
            break;
        case TIMER_REG_COUNTER:
            *out_val = st->counter;
            break;
        default:
            *out_val = 0;
            break;
    }
    return 0;
}

static int timer_write(void* ctx, uint64_t offset, uint32_t size, uint64_t val) {
    TimerState* st = (TimerState*)ctx;
    if (!st || size != 4) return 0;
    const uint32_t v32 = (uint32_t)val;

    switch (offset) {
        case TIMER_REG_CONTROL:
            st->control = v32 & (CTRL_ENABLE | CTRL_INT_ENABLE);
            if ((st->control & CTRL_INT_ENABLE) == 0 && st->services) {
                st->services->set_irq(st->services->host_ctx, st->irq_num, 0);
            }
            break;
        case TIMER_REG_STATUS:
            /* Write-1-to-clear status bit */
            st->status &= ~v32;
            if ((st->status & STAT_EXPIRED) == 0 && st->services) {
                st->services->set_irq(st->services->host_ctx, st->irq_num, 0);
            }
            break;
        case TIMER_REG_LIMIT:
            st->limit = v32;
            st->counter = 0;
            break;
        default:
            break;
    }
    return 0;
}

static void timer_advance_cycles(void* ctx, uint64_t cycles) {
    TimerState* st = (TimerState*)ctx;
    if (!st || (st->control & CTRL_ENABLE) == 0 || st->limit == 0) return;

    st->counter += (uint32_t)cycles;
    if (st->counter >= st->limit) {
        st->counter = 0;
        st->status |= STAT_EXPIRED;
        if (st->services) {
            if (st->services->log_msg) {
                st->services->log_msg(st->services->host_ctx, SIMRV_LOG_INFO,
                                      "[timer-plugin] Timer expired; raising interrupt");
            }
            if ((st->control & CTRL_INT_ENABLE) != 0 && st->services->set_irq) {
                st->services->set_irq(st->services->host_ctx, st->irq_num, 1);
            }
        }
    }
}

static void timer_destroy(void* ctx) { free(ctx); }

static void timer_set_host_services(void* ctx, const SimRvHostServices* services) {
    TimerState* st = (TimerState*)ctx;
    if (st) st->services = services;
}

static const SimRvExternalDevice kTimerDevice = {
    .api_version = SIMRV_EXTERNAL_DEVICE_API_VERSION,
    .name = "demo-timer",
    .compatible = "simrv,demo-timer",
    .default_base = 0x10030000ULL,
    .default_size = 0x1000ULL,
    .init = timer_init,
    .reset = timer_reset,
    .read = timer_read,
    .write = timer_write,
    .destroy = timer_destroy,
    .set_host_services = timer_set_host_services,
    .advance_cycles = timer_advance_cycles,
};

const SimRvExternalDevice* simrv_device_plugin_init(void) { return &kTimerDevice; }
