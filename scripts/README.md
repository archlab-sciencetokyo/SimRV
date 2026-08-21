# SimRV User Scripts & Utilities

This directory contains command-line utilities and benchmarking frameworks for running and evaluating **SimRV**.

---

## 1. Benchmarking & Architecture Comparison

### `compare_arch.py` (32-bit vs. 64-bit Comparison)
Compares execution metrics (instruction count, wall-clock time, simulation MIPS, Spike speedup, and multi-word arithmetic overhead) side-by-side between 32-bit (`RV32`) and 64-bit (`RV64`) targets:

```bash
# Compare 32-bit vs 64-bit for aha-mont
python3 scripts/compare_arch.py --benchmark aha-mont --runs 5

# Custom binary paths and instruction limits
python3 scripts/compare_arch.py \
  --benchmark aha-mont \
  --simrv64 ./SimRV \
  --simrv32 /path/to/rv32/SimRV \
  --spike spike \
  --runs 5 \
  --limit 50000000 \
  --json report_arch.json
```

### `benchmark.py` (Comprehensive Benchmark & Spike Comparison)
Runs standalone benchmarks with statistical variance analysis (CI95, std dev), peak RSS memory tracking, and side-by-side performance comparisons vs. Spike:

```bash
# Run a single benchmark against Spike
python3 scripts/benchmark.py \
  --simrv ./SimRV \
  --spike spike \
  --test aha-mont \
  --runs 5 \
  --json report.json \
  --markdown report.md

# Run the realworld benchmark suite
python3 scripts/benchmark.py \
  --simrv ./SimRV \
  --spike spike \
  --suite realworld \
  --runs 3
```

### `run_benchmarks.py` (Extended Dual-Architecture Suite)
Executes the full test suite against Spike across both RV64 and RV32, printing a consolidated summary and 32-bit vs. 64-bit architectural comparison table:

```bash
python3 scripts/run_benchmarks.py --runs 3 --json extended_report.json
```

### `benchmark.sh` (Quick Shell Wrapper)
Convenience script for running quick benchmarks with environment overrides:

```bash
SIMRV_BENCH_TEST=aha-mont SIMRV_BENCH_ITERS=5 ./scripts/benchmark.sh
```

### `compare_benchmarks.py` (Baseline vs. Candidate Regression Analysis)
Calculates geometric-mean throughput change between two frozen benchmark JSON reports:

```bash
python3 scripts/compare_benchmarks.py baseline.json candidate.json
```

---

## 2. Linux Image Generation

### `build-linux-image.sh`
Builds reproducible Linux kernel (`fw_payload.bin`), device tree (`devicetree.dtb`), and root filesystem (`root.bin`) images for either `rv64` or `rv32`:

```bash
# Build RV64 Linux image
bash scripts/build-linux-image.sh --arch rv64

# Build RV32 Linux image
bash scripts/build-linux-image.sh --arch rv32
```
