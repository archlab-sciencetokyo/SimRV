/**
 * @file simrv_dma.h
 * @brief Freestanding C/C++ header for the SimRV generic MMIO DMA Controller.
 *
 * This header contains pure C declarations, register offsets, bitmasks, and
 * inline accessors suitable for baremetal firmware, RTOS kernels (FreeRTOS,
 * Zephyr), and non-Linux operating systems (xv6, seL4, etc.).
 */
#ifndef SIMRV_DEVICE_SIMRV_DMA_H
#define SIMRV_DEVICE_SIMRV_DMA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Default physical memory map base address and IRQ */
#define SIMRV_DMA_BASE_ADDR 0x10009000ULL
#define SIMRV_DMA_SIZE 0x1000ULL
#define SIMRV_DMA_IRQ 12U

/* Register Offsets (32-bit aligned) */
#define SIMRV_DMA_REG_CONTROL 0x00U
#define SIMRV_DMA_REG_STATUS 0x04U
#define SIMRV_DMA_REG_SRC_ADDR_LO 0x08U
#define SIMRV_DMA_REG_SRC_ADDR_HI 0x0CU
#define SIMRV_DMA_REG_DST_ADDR_LO 0x10U
#define SIMRV_DMA_REG_DST_ADDR_HI 0x14U
#define SIMRV_DMA_REG_BYTE_COUNT 0x18U
#define SIMRV_DMA_REG_INTERRUPT_ACK 0x1CU

/* CONTROL Register Bitmasks */
#define SIMRV_DMA_CTRL_START (1U << 0)      /* Write 1 to start transfer (self-clearing) */
#define SIMRV_DMA_CTRL_INT_ENABLE (1U << 1) /* 1 = raise IRQ on completion or error */
#define SIMRV_DMA_CTRL_BUSY (1U << 2)       /* Read-only: 1 = transfer currently active */

/* STATUS Register Bitmasks */
#define SIMRV_DMA_STAT_DONE (1U << 0)  /* 1 = transfer completed successfully */
#define SIMRV_DMA_STAT_ERROR (1U << 1) /* 1 = transfer encountered an error */

/**
 * @brief Freestanding MMIO register accessor helpers for baremetal / guest code.
 */
static inline volatile uint32_t* simrv_dma_reg(uintptr_t base, uint32_t offset) {
    return (volatile uint32_t*)(base + offset);
}

static inline void simrv_dma_start_transfer(uintptr_t base, uint64_t src, uint64_t dst,
                                            uint32_t bytes, int enable_interrupt) {
    *simrv_dma_reg(base, SIMRV_DMA_REG_SRC_ADDR_LO) = (uint32_t)(src & 0xFFFFFFFFU);
    *simrv_dma_reg(base, SIMRV_DMA_REG_SRC_ADDR_HI) = (uint32_t)(src >> 32U);
    *simrv_dma_reg(base, SIMRV_DMA_REG_DST_ADDR_LO) = (uint32_t)(dst & 0xFFFFFFFFU);
    *simrv_dma_reg(base, SIMRV_DMA_REG_DST_ADDR_HI) = (uint32_t)(dst >> 32U);
    *simrv_dma_reg(base, SIMRV_DMA_REG_BYTE_COUNT) = bytes;

    uint32_t ctrl = SIMRV_DMA_CTRL_START;
    if (enable_interrupt) {
        ctrl |= SIMRV_DMA_CTRL_INT_ENABLE;
    }
    *simrv_dma_reg(base, SIMRV_DMA_REG_CONTROL) = ctrl;
}

static inline int simrv_dma_is_busy(uintptr_t base) {
    return (*simrv_dma_reg(base, SIMRV_DMA_REG_CONTROL) & SIMRV_DMA_CTRL_BUSY) != 0;
}

static inline int simrv_dma_is_done(uintptr_t base) {
    return (*simrv_dma_reg(base, SIMRV_DMA_REG_STATUS) & SIMRV_DMA_STAT_DONE) != 0;
}

static inline int simrv_dma_is_error(uintptr_t base) {
    return (*simrv_dma_reg(base, SIMRV_DMA_REG_STATUS) & SIMRV_DMA_STAT_ERROR) != 0;
}

static inline void simrv_dma_ack_interrupt(uintptr_t base) {
    *simrv_dma_reg(base, SIMRV_DMA_REG_INTERRUPT_ACK) = 1U;
}

/**
 * @brief Synchronous / blocking DMA copy helper for baremetal code.
 *
 * @param base Base address of DMA controller (e.g. SIMRV_DMA_BASE_ADDR).
 * @param dst Destination physical address.
 * @param src Source physical address.
 * @param bytes Number of bytes to copy.
 * @return 0 on success, -1 on error.
 */
static inline int simrv_dma_memcpy(uintptr_t base, void* dst, const void* src, size_t bytes) {
    simrv_dma_ack_interrupt(base);
    simrv_dma_start_transfer(base, (uintptr_t)src, (uintptr_t)dst, (uint32_t)bytes, 0);
    while (simrv_dma_is_busy(base)) {
        /* Busy wait */
    }
    if (simrv_dma_is_error(base)) {
        simrv_dma_ack_interrupt(base);
        return -1;
    }
    simrv_dma_ack_interrupt(base);
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* SIMRV_DEVICE_SIMRV_DMA_H */
