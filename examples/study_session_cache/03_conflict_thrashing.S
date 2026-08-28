# ==============================================================================
# SimRV Cache Study Session - Exercise 3: Conflict Misses & LRU Thrashing
# ==============================================================================
# Architecture: RV32GC / Bare-metal
# Base Address: 0x80000000
#
# Concept:
#   SimRV L1 DCache configuration:
#     - Block size = 32 bytes (offset: bits [4:0])
#     - Number of sets = 16 (index: bits [8:5])
#     - Associativity = 4 ways per set
#
#   Set Index stride = 16 sets * 32 bytes = 512 bytes (0x200).
#   Any address sharing (Addr >> 5) & 0xF maps to the exact same cache set.
#
#   5 addresses mapping to Set 0:
#     Addr 0: buffer_base + 0x000  (Set 0)
#     Addr 1: buffer_base + 0x200  (Set 0)
#     Addr 2: buffer_base + 0x400  (Set 0)
#     Addr 3: buffer_base + 0x600  (Set 0)
#     Addr 4: buffer_base + 0x800  (Set 0)
#
#   When looping over these 5 addresses:
#     - Initial 4 accesses fill Ways 0, 1, 2, 3 of Set 0.
#     - The 5th access causes a conflict miss and evicts the LRU way (Way 0).
#     - In subsequent loop iterations, every single access is a conflict miss!
# ==============================================================================

.section .text
.global _start

_start:
    la a0, buffer_base      # Base address (aligned to 512 bytes)
    li a1, 3                # Loop 3 times over the 5 conflicting addresses

thrash_loop:
    beqz a1, done

    # Access 1: Set 0
    lw t0, 0x000(a0)

    # Access 2: Set 0 (Stride +512)
    lw t1, 0x200(a0)

    # Access 3: Set 0 (Stride +1024)
    lw t2, 0x400(a0)

    # Access 4: Set 0 (Stride +1536)
    lw t3, 0x600(a0)

    # Access 5: Set 0 (Stride +2048) -> Exceeds 4 ways in Set 0 -> Forces LRU Eviction!
    addi t5, a0, 0x400
    lw t4, 0x400(t5)

    addi a1, a1, -1
    j thrash_loop

done:
    j done                  # Pause here for TUI inspection

.section .data
.align 9                    # Align to 512-byte (2^9) boundary so Set Index = 0
buffer_base:
    .space 3000, 0xAA       # Reserve 3 KB buffer
