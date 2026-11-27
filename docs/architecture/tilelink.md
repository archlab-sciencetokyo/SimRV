# TileLink-C profile and MESI policy

SimRV models an internal TileLink-C 1.8.1 profile with an 8-byte beat and 64-byte cache block on
both RV32 and RV64. It is not an external RTL or wire-level interoperability interface.

The selected profile controls the manager capabilities. TileLink-C coherent RAM supports
AcquireBlock, AcquirePerm, Get, PutFullData, PutPartialData, and logical and arithmetic atomic
operations. TileLink-UH and TileLink-UL are uncached; UL has atomics disabled by its default
specification. MMIO declares uncached Get and Put operations only. Logical OR is used for page-table
A/D updates.

The protocol boundary uses typed A–E opcodes and typed Grow, Cap, and Report parameters. The
cycle-level interface implements ready/valid handshakes, held responses under backpressure,
multi-beat data transfers, source tracking, B-channel probes, C-channel probe acknowledgements and
releases, and E-channel GrantAck. Grants use live nonzero sink IDs until GrantAck. Corrupt dirty
probe data completes with a denied, corrupt grant response. The checker enforces size, alignment,
byte masks, source reuse, response matching, and sink lifetime rules.

This profile fixes data beats at 8 bytes and coherent cache blocks at 64 bytes. It models a single
serialized burst per channel and does not claim RTL pin-level interoperability. Uncached MMIO does
not expose atomics or coherence operations.

Cache policy is Illinois MESI:

| MESI cache state | TileLink permission |
| --- | --- |
| Invalid | None |
| Shared | Branch |
| Exclusive (clean) | Trunk |
| Modified (dirty) | Trunk |

The directory supports up to 64 harts with a 64-bit sharer mask and optional exclusive owner.
Instruction fills participate coherently. Device DMA synchronizes dirty owners and invalidates
cached copies before host-memory access. Clean and dirty evictions issue Release or ReleaseData and
receive ReleaseAck.

Qualification claims are tracked separately: RISC-V architectural evidence belongs in
[RISC-V compliance scope](compliance.md), while this document describes only the declared
TileLink-C profile and MESI policy.
