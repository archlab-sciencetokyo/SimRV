# SimRV 2.1 Release Guide

The authoritative release contract is [`release/release-manifest.json`](../release/release-manifest.json).
SimRV 2.1 officially supports Linux x86-64 hosts using GCC 15+ or Clang 20+, with separate
RV32GCBV and RV64GCBV binaries. Spike lockstep and the GDB server are optional features.

## Support and qualification boundary

“Supported” means the release matrix contains passing evidence. “Partial” means useful behavior
has a documented qualification gap. These labels are not RISC-V certification claims.

| Area | Status | Evidence or boundary |
| --- | --- | --- |
| RV32I/RV64I, M, A, C | Supported | Semantic tests, `riscv-tests`, and Spike lockstep |
| Zba/Zbb/Zbc/Zbs | Supported | ISA and lockstep suites |
| F/D | Partial | RMM arithmetic remains unqualified |
| RVV 1.0 | Partial | Advertised instruction subset at VLEN 256; no complete-V claim |
| Privilege, traps, CSRs; Sv32/Sv39/Sv48 | Supported | Semantic tests and Linux boot |
| Direct SBI & Multi-Hart OpenSBI | Supported | Platform, SMP HSM/IPI, and Linux integration tests |
| UART, timers, ACLINT, PLIC, AIA | Supported | Platform devices and interrupt delivery tests |
| PCIe Root Complex & VirtIO (MMIO/PCI) | Supported | Device framework, block/net/console/gpu/input/sound tests |
| TileLink-C Directory Coherence (MESI) | Supported | Multi-hart cache coherence regression tests |
| TUI Framework & Modals | Supported | Native UI framework tests and Linux PTY interaction |
| GDB RSP | Optional | Debug integration; not a complete protocol promise |
| Spike lockstep | Optional dependency | Reference evidence when Spike is provisioned |
| Linux x86-64 host | Supported | GCC 15+ and Clang 20+ release matrix |

> [!NOTE]
> Rollback snapshots and reverse stepping were permanently removed in SimRV 2.1.0 to eliminate state copy overhead and streamline pipeline kernels.

The detailed architectural boundary and known deviations are in
[RISC-V compliance scope](RISCV_COMPLIANCE.md). The release manifest controls when prose and
machine-readable requirements disagree.

## Local release validation

Validate metadata and an existing binary:

```bash
python3 scripts/release_check.py --binary build/rv64-release/SimRV
```

Run clean builds and gate suites for both compilers and both guest XLENs:

```bash
python3 scripts/reproduce.py --mode full --output repro/results
```

The maintained validation preference is stable Clang 21+ and GCC 15+. GCC 16.2 is a current
stable upstream series; LLVM 23 is still pre-release, so use stable LLVM 22.x (or the validated
Clang 21.x host package) for release evidence. `rv64-clang-release` and `rv64-gcc-release`
select the corresponding compiler from `PATH` without changing the portable default presets.

The gate writes versioned evidence with compiler, architecture, MISA/VLEN, dependency revisions,
test counts, skips, elapsed time, and binary size. A required suite that is absent or skipped is a
failure, never a pass.
Release CI additionally runs sanitizer checks, builds Doxygen output, validates archives and
checksums, extracts packaged binaries, and repeats CLI version/help smoke tests.

## Performance acceptance

Benchmark reports use an unmeasured warmup, at least five measured samples, median throughput,
and recorded host metadata. Compare a frozen baseline with a candidate:

```bash
python3 scripts/compare_benchmarks.py baseline.json candidate.json
```

Performance is evidence-only: comparisons never block a release unless `--enforce` is explicitly
requested for an individual study. Report effect sizes and variability without treating a speedup
target as a correctness criterion. Results are comparable only on the same idle host, compiler,
build configuration, guest binaries, and instruction limits.

Pass `--perf` to `scripts/benchmark.py` (or set `SIMRV_CA_BENCH_PERF=1` for
`benchmark-ca.sh`) to add per-sample Linux host counters. Wall time remains the primary throughput
measurement; host cycles, instructions, branches, and cache events are supplemental diagnostics.
Reports retain perf's enabled time and running percentage, so multiplexed or unsupported counters
can be identified rather than compared as if they were exact. For publication runs, pin the process
to one physical core and keep the CPU governor, kernel, perf event set, and host hardware constant.

## Required evidence

The manifest enumerates the authoritative matrix. Native regression, advertised ISA, vector,
Linux/PTy lifecycle, sanitizer, and packaging suites must all be present and pass with zero skips.
External inputs are pinned but not redistributed; their observed revisions are stored in the
evidence report. See [`../repro/README.md`](../repro/README.md) for the clean-checkout workflow.

## Publishing

The release tag, CMake version, changelog heading, manifest, binary-reported version, asset names,
and checksums must agree. Release automation publishes RV32/RV64 GCC-built headless binaries and a
separate generated API-documentation archive, reproducibility bundle, and evidence JSON. Generated
Doxygen HTML is never committed. The final candidate requires two consecutive complete CI runs
without blocker fixes or changes to the release contract.
