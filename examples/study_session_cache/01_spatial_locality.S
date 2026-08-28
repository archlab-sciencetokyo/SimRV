# ==============================================================================
# SimRV Cache Study Session - Exercise 1: Spatial Locality & Compulsory Misses
# ==============================================================================
# Architecture: RV32GC / Bare-metal
# Base Address: 0x80000000
#
# Concept:
#   A cache line is 32 bytes (8 words).
#   The first access to address 0x80001000 causes a compulsory (cold) miss.
#   SimRV loads the entire 32-byte block from memory into DCache Set 0.
#   Subsequent 7 word loads (offset +4, +8, +12, +16, +20, +24, +28) HIT.
#   The 9th load accesses 0x80001020 (offset +32), crossing the cache line
#   boundary and causing a new compulsory miss into Set 1.
# ==============================================================================

.section .text
.global _start

_start:
    # Initialize base pointer to data buffer in a0
    la a0, test_data_buffer

    # --- Block 0 (32-byte line at test_data_buffer) ---
    lw t0, 0(a0)    # Access offset +0  -> MISS (Compulsory / Cold)
    lw t1, 4(a0)    # Access offset +4  -> HIT  (Spatial Locality)
    lw t2, 8(a0)    # Access offset +8  -> HIT  (Spatial Locality)
    lw t3, 12(a0)   # Access offset +12 -> HIT  (Spatial Locality)
    lw t4, 16(a0)   # Access offset +16 -> HIT  (Spatial Locality)
    lw t5, 20(a0)   # Access offset +20 -> HIT  (Spatial Locality)
    lw t6, 24(a0)   # Access offset +24 -> HIT  (Spatial Locality)
    lw s1, 28(a0)   # Access offset +28 -> HIT  (Spatial Locality)

    # --- Block 1 (Next 32-byte line at test_data_buffer + 32) ---
    lw s2, 32(a0)   # Access offset +32 -> MISS (Line boundary crossed)
    lw s3, 36(a0)   # Access offset +36 -> HIT  (Spatial Locality)

done:
    j done          # Infinite loop - pause here for TUI inspection

.section .data
.align 5            # Align to 32-byte (2^5) cache line boundary
test_data_buffer:
    .word 0x11111111, 0x22222222, 0x33333333, 0x44444444
    .word 0x55555555, 0x66666666, 0x77777777, 0x88888888
    # Second cache line
    .word 0xAAAAAAAA, 0xBBBBBBBB, 0xCCCCCCCC, 0xDDDDDDDD
    .word 0xEEEEEEEE, 0xFFFFFFFF, 0x12345678, 0x9ABCDEF0
