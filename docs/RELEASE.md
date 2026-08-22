# SimRV 2.0 Release Guide

The authoritative release contract is [`release/release-manifest.json`](../release/release-manifest.json).
SimRV 2.0 officially supports Linux x86-64 hosts using GCC 14+ or Clang 20+, with separate
RV32GCBV and RV64GCBV binaries. Spike lockstep and the GDB server are optional features.

## Local release validation

Validate metadata and an existing binary:

```bash
python3 scripts/release_check.py --binary build/rv64-release/SimRV
```

Run clean builds and gate suites for both compilers and both guest XLENs:

```bash
python3 scripts/reproduce.py --mode full --output repro/results
```

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
