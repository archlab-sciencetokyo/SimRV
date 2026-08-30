#!/usr/bin/env bash
# Capture a repeatable fast cycle-engine baseline with the standard benchmark reporter.
set -euo pipefail

simrv_bin="${SIMRV_BIN:-./build/rv64-release/SimRV}"
test_target="${SIMRV_CA_BENCH_TEST:-coremark}"
runs="${SIMRV_CA_BENCH_ITERS:-5}"
limit="${SIMRV_CA_BENCH_LIMIT:-20000000}"
report="${SIMRV_CA_BENCH_REPORT:-benchmark-ca.json}"
affinity="${SIMRV_CA_BENCH_AFFINITY:-}"
perf_args=()
if [[ "${SIMRV_CA_BENCH_PERF:-0}" == "1" ]]; then
  perf_args+=(--perf)
fi

runner=()
if [[ -n "$affinity" ]]; then
  if ! command -v taskset >/dev/null 2>&1; then
    echo "SIMRV_CA_BENCH_AFFINITY requires taskset" >&2
    exit 2
  fi
  runner=(taskset -c "$affinity")
fi

"${runner[@]}" python3 scripts/benchmark.py \
  --simrv "$simrv_bin" \
  --test "$test_target" \
  --runs "$runs" \
  --limit "$limit" \
  --json "$report" \
  --compare-instruction \
  "${perf_args[@]}" \
  --simrv-arg=--mode --simrv-arg=cycle-accurate
