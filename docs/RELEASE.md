# SimRV 2.0 Release Guide

The authoritative release contract is [`release/release-manifest.json`](../release/release-manifest.json).
SimRV 2.0 officially supports Linux x86-64 hosts using GCC 14+ or Clang 20+, with separate
RV32GCBV and RV64GCBV binaries. SDL3, Spike lockstep, and the GDB server are optional features.

## Local release validation

Validate metadata and an existing binary:

```bash
python3 scripts/release_check.py --binary build/rv64-release/SimRV
```

Run two clean builds and gate suites for both compilers and both guest XLENs:

```bash
python3 scripts/release_gate.py --iterations 2
```

The gate writes `release-report.json` with compiler, architecture, elapsed time, and binary size.
Release CI additionally runs sanitizer checks, builds Doxygen output, validates archives and
checksums, extracts packaged binaries, and repeats CLI version/help smoke tests.

## Performance acceptance

Benchmark reports use an unmeasured warmup, at least five measured samples, median throughput,
and recorded host metadata. Compare a frozen baseline with a candidate:

```bash
python3 scripts/compare_benchmarks.py baseline.json candidate.json
```

The candidate must improve geometric-mean throughput by at least 5% and may not regress any
individual workload by more than 3%. Results are comparable only on the same idle host, compiler,
build configuration, guest binaries, and instruction limits.

## Publishing

The release tag, CMake version, changelog heading, manifest, binary-reported version, asset names,
and checksums must agree. Release automation publishes RV32/RV64 GCC-built headless binaries and a
separate generated API-documentation archive. Generated Doxygen HTML is never committed.
