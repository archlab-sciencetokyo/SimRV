# TileLink-C profile and MESI policy

SimRV models an internal TileLink-C 1.8.1 profile with an 8-byte beat and 32-byte cache block on
both RV32 and RV64. It is not an external RTL or wire-level interoperability interface.

Coherent RAM declares AcquireBlock, AcquirePerm, Get, PutFullData, PutPartialData, and Logical OR
(used for page-table A/D updates). MMIO declares uncached Get and Put operations only. Unsupported
Arithmetic, Hint/Intent, and Logical variants return a legal denied response.

The protocol boundary uses typed A–E opcodes and typed Grow, Cap, and Report parameters. Requests
are source-tracked, grants use live nonzero sink IDs until GrantAck, and responses carry distinct
`denied` and `corrupt` indications. The checker enforces size, alignment, byte mask, source reuse,
response matching, and sink lifetime rules.

Cache policy is Illinois MESI:

| MESI cache state | TileLink permission |
|---|---|
| Invalid | None |
| Shared | Branch |
| Exclusive (clean) | Trunk |
| Modified (dirty) | Trunk |

The directory supports up to 64 harts with a 64-bit sharer mask and optional exclusive owner.
Instruction fills participate coherently. Device DMA synchronizes dirty owners and invalidates
cached copies before host-memory access. Clean and dirty evictions issue Release or ReleaseData and
receive ReleaseAck.

Qualification claims are tracked separately: RISC-V architectural evidence belongs in
[RISCV_COMPLIANCE.md](RISCV_COMPLIANCE.md), while this document describes only the declared
TileLink-C profile and MESI policy.
