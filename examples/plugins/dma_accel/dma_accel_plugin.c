/**
 * @file dma_accel_plugin.c
 * @brief Example SimRV dynamic external device plugin: DMA memory accelerator.
 *
 * Demonstrates:
 * - Version 2 C ABI (SIMRV_EXTERNAL_DEVICE_API_VERSION = 2)
 * - Guest physical memory bus-mastering via `SimRvHostServices::dma_read` and `dma_write`
 * - Platform interrupt generation on transfer completion
 * - Host cycle query via `SimRvHostServices::get_cycle_count`
 * - Structured simulator logging via `SimRvHostServices::log_msg`
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "simrv/device/ExternalDevice.h"

#define DMA_REG_CONTROL 0x00U  /* Bit 0: Start, Bit 1: IRQ Enable, Bit 2: Invert bytes */
#define DMA_REG_STATUS 0x04U   /* Bit 0: Done, Bit 1: Error, Bit 2: Busy (W1C) */
#define DMA_REG_SRC_LO 0x08U   /* Source 64-bit physical address low */
#define DMA_REG_SRC_HI 0x0CU   /* Source 64-bit physical address high */
#define DMA_REG_DST_LO 0x10U   /* Destination 64-bit physical address low */
#define DMA_REG_DST_HI 0x14U   /* Destination 64-bit physical address high */
#define DMA_REG_LENGTH 0x18U   /* Byte transfer count */
#define DMA_REG_CHECKSUM 0x1CU /* Computed 32-bit checksum of processed bytes */

#define CTRL_START (1U << 0)
#define CTRL_INT_ENABLE (1U << 1)
#define CTRL_INVERT_BYTES (1U << 2)

#define STAT_DONE (1U << 0)
#define STAT_ERROR (1U << 1)
#define STAT_BUSY (1U << 2)

typedef struct DmaAccelState {
    const SimRvHostServices* services;
    uint32_t control;
    uint32_t status;
    uint64_t src_addr;
    uint64_t dst_addr;
    uint32_t length;
    uint32_t checksum;
    uint32_t irq_num;
} DmaAccelState;

static int dma_init(void** out_ctx, const char* args) {
    DmaAccelState* st = (DmaAccelState*)calloc(1, sizeof(DmaAccelState));
    if (!st) return -1;
    st->irq_num = 15; /* Default IRQ line */
    if (args && *args != '\0') {
        int parsed = atoi(args);
        if (parsed > 0) st->irq_num = (uint32_t)parsed;
    }
    *out_ctx = st;
    return 0;
}

static void dma_reset(void* ctx) {
    DmaAccelState* st = (DmaAccelState*)ctx;
    if (!st) return;
    st->control = 0;
    st->status = 0;
    st->src_addr = 0;
    st->dst_addr = 0;
    st->length = 0;
    st->checksum = 0;
    if (st->services && st->services->set_irq) {
        st->services->set_irq(st->services->host_ctx, st->irq_num, 0);
    }
}

static void dma_execute_transfer(DmaAccelState* st) {
    if (!st || !st->services || !st->services->dma_read || !st->services->dma_write) {
        st->status |= STAT_ERROR;
        return;
    }

    if (st->length == 0) {
        st->status |= STAT_ERROR;
        return;
    }

    uint8_t* buffer = (uint8_t*)malloc(st->length);
    if (!buffer) {
        st->status |= STAT_ERROR;
        return;
    }

    /* 1. Read input buffer directly from guest physical RAM */
    if (st->services->dma_read(st->services->host_ctx, st->src_addr, buffer, st->length) != 0) {
        st->status |= STAT_ERROR;
        free(buffer);
        return;
    }

    /* 2. Process bytes and compute running checksum */
    uint32_t sum = 0;
    const int invert = (st->control & CTRL_INVERT_BYTES) != 0;
    for (uint32_t i = 0; i < st->length; ++i) {
        if (invert) buffer[i] = ~buffer[i];
        sum = (sum * 31U) + buffer[i];
    }
    st->checksum = sum;

    /* 3. Write transformed buffer back to guest physical RAM if destination specified */
    if (st->dst_addr != 0) {
        if (st->services->dma_write(st->services->host_ctx, st->dst_addr, buffer, st->length) !=
            0) {
            st->status |= STAT_ERROR;
            free(buffer);
            return;
        }
    }

    free(buffer);
    st->status |= STAT_DONE;

    if (st->services->log_msg) {
        char msg[128];
        snprintf(msg, sizeof(msg), "[dma-accel] DMA transfer complete: %u bytes, checksum: 0x%08x",
                 st->length, st->checksum);
        st->services->log_msg(st->services->host_ctx, SIMRV_LOG_INFO, msg);
    }

    if ((st->control & CTRL_INT_ENABLE) != 0 && st->services->set_irq) {
        st->services->set_irq(st->services->host_ctx, st->irq_num, 1);
    }
}

static int dma_read(void* ctx, uint64_t offset, uint32_t size, uint64_t* out_val) {
    DmaAccelState* st = (DmaAccelState*)ctx;
    if (!st || !out_val || size != 4) {
        if (out_val) *out_val = 0;
        return 0;
    }

    switch (offset) {
        case DMA_REG_CONTROL:
            *out_val = st->control;
            break;
        case DMA_REG_STATUS:
            *out_val = st->status;
            break;
        case DMA_REG_SRC_LO:
            *out_val = (uint32_t)(st->src_addr & 0xFFFFFFFFU);
            break;
        case DMA_REG_SRC_HI:
            *out_val = (uint32_t)(st->src_addr >> 32U);
            break;
        case DMA_REG_DST_LO:
            *out_val = (uint32_t)(st->dst_addr & 0xFFFFFFFFU);
            break;
        case DMA_REG_DST_HI:
            *out_val = (uint32_t)(st->dst_addr >> 32U);
            break;
        case DMA_REG_LENGTH:
            *out_val = st->length;
            break;
        case DMA_REG_CHECKSUM:
            *out_val = st->checksum;
            break;
        default:
            *out_val = 0;
            break;
    }
    return 0;
}

static int dma_write(void* ctx, uint64_t offset, uint32_t size, uint64_t val) {
    DmaAccelState* st = (DmaAccelState*)ctx;
    if (!st || size != 4) return 0;
    const uint32_t v32 = (uint32_t)val;

    switch (offset) {
        case DMA_REG_CONTROL:
            st->control = v32;
            if ((v32 & CTRL_START) != 0) {
                dma_execute_transfer(st);
            }
            break;
        case DMA_REG_STATUS:
            st->status &= ~v32;
            if ((st->status & STAT_DONE) == 0 && st->services && st->services->set_irq) {
                st->services->set_irq(st->services->host_ctx, st->irq_num, 0);
            }
            break;
        case DMA_REG_SRC_LO:
            st->src_addr = (st->src_addr & 0xFFFFFFFF00000000ULL) | v32;
            break;
        case DMA_REG_SRC_HI:
            st->src_addr = (st->src_addr & 0x00000000FFFFFFFFULL) | ((uint64_t)v32 << 32U);
            break;
        case DMA_REG_DST_LO:
            st->dst_addr = (st->dst_addr & 0xFFFFFFFF00000000ULL) | v32;
            break;
        case DMA_REG_DST_HI:
            st->dst_addr = (st->dst_addr & 0x00000000FFFFFFFFULL) | ((uint64_t)v32 << 32U);
            break;
        case DMA_REG_LENGTH:
            st->length = v32;
            break;
        default:
            break;
    }
    return 0;
}

static void dma_destroy(void* ctx) { free(ctx); }

static void dma_set_host_services(void* ctx, const SimRvHostServices* services) {
    DmaAccelState* st = (DmaAccelState*)ctx;
    if (st) st->services = services;
}

static const SimRvExternalDevice kDmaDevice = {
    .api_version = SIMRV_EXTERNAL_DEVICE_API_VERSION,
    .name = "demo-dma-accel",
    .compatible = "simrv,demo-dma-accel",
    .default_base = 0x10040000ULL,
    .default_size = 0x1000ULL,
    .init = dma_init,
    .reset = dma_reset,
    .read = dma_read,
    .write = dma_write,
    .destroy = dma_destroy,
    .set_host_services = dma_set_host_services,
    .advance_cycles = NULL,
};

const SimRvExternalDevice* simrv_device_plugin_init(void) { return &kDmaDevice; }
