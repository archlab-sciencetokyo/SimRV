# SimRV 2.0 Support and Qualification Matrix

“Supported” means the release matrix contains passing evidence. “Partial” means useful behavior is
implemented with a documented gap. “Optional” means the integration is tested only when its pinned
external dependency is provisioned. These labels are not RISC-V certification claims.

| Area | Status | Release evidence / boundary |
| --- | --- | --- |
| RV32I/RV64I, M, A, C | Supported | Semantic tests, `riscv-tests`, and Spike lockstep |
| Zba/Zbb/Zbc/Zbs | Supported | ISA and lockstep suites |
| F/D | Partial | RMM arithmetic remains unqualified; see compliance scope |
| RVV 1.0 | Partial | Advertised instruction subset at VLEN 256; no complete-V claim |
| M/S/U privilege, traps, CSRs | Supported | Semantic tests and Linux boot |
| Sv32/Sv39/Sv48 | Supported | MMU semantics and Linux boot |
| Direct SBI | Supported | Single-hart Base/TIME/RFENCE/IPI/SRST boundary |
| OpenSBI/Linux | Supported | Pinned images and PTY lifecycle tests for RV32/RV64 |
| UART, CLINT, PLIC, virtio disk | Supported | Semantic and Linux integration tests |
| Framebuffer/audio/input | Supported | MMIO device models and terminal TUI/Sixel display |
| GDB RSP | Optional | Debug integration; no promise of complete protocol coverage |
| Spike lockstep | Optional dependency | Required reference evidence when Spike is provisioned |
| TUI | Supported | Parser, input, rendering, and Linux PTY interaction tests |
| Linux x86-64 host | Supported | GCC 14+ and Clang 20+ release matrix |
| Other hosts/toolchains | Unsupported | Community use only; no 2.0 release qualification |

The detailed architectural contract and known deviations are maintained in
[`RISCV_COMPLIANCE.md`](RISCV_COMPLIANCE.md). Machine-readable requirements are maintained in
`release/release-manifest.json`; when prose and the manifest disagree, the manifest controls the
release gate and the prose must be corrected.
