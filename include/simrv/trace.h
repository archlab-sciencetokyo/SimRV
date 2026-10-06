/**
 * @file trace.h
 * @brief Guest-side helpers for the SimRV Xsimrvtrace CSR annotation ABI.
 *
 * The helpers are no-ops on non-RISC-V hosts. On RISC-V they require the custom
 * custom U-level CSR at 0x800 and the Zicsr extension.
 */
#ifndef SIMRV_TRACE_H
#define SIMRV_TRACE_H

#include <stdint.h>

#if defined(__riscv)
static inline void simrv_trace_write(uintptr_t value) {
    __asm__ volatile("csrw 0x800, %0" : : "r"(value) : "memory");
}

static inline void simrv_trace_emit(const char *label, uintptr_t command) {
    const unsigned char *byte = (const unsigned char *)label;
    while (*byte != 0) {
        simrv_trace_write((uintptr_t)*byte++);
    }
    simrv_trace_write(command);
}

static inline void simrv_trace_begin(const char *name) {
    simrv_trace_emit(name, (uintptr_t)0x40000000u);
}

static inline void simrv_trace_end(const char *name) {
    simrv_trace_emit(name, (uintptr_t)0x80000000u);
}

static inline void simrv_marker(const char *message) {
    simrv_trace_emit(message, (uintptr_t)0xC0000000u);
}
#else
static inline void simrv_trace_begin(const char *name) { (void)name; }
static inline void simrv_trace_end(const char *name) { (void)name; }
static inline void simrv_marker(const char *message) { (void)message; }
#endif

#endif  // SIMRV_TRACE_H
