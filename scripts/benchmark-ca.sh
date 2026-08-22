#!/usr/bin/env bash
# Capture a repeatable fast cycle-engine baseline with the standard benchmark reporter.
set -euo pipefail

simrv_bin="${SIMRV_BIN:-./build/rv64-release/SimRV}"
test_target="${SIMRV_CA_BENCH_TEST:-coremark}"
runs="${SIMRV_CA_BENCH_ITERS:-5}"
limit="${SIMRV_CA_BENCH_LIMIT:-20000000}"
report="${SIMRV_CA_BENCH_REPORT:-benchmark-ca.json}"
perf_args=()
if [[ "${SIMRV_CA_BENCH_PERF:-0}" == "1" ]]; then
  perf_args+=(--perf)
fi

python3 scripts/benchmark.py \
  --simrv "$simrv_bin" \
  --test "$test_target" \
  --runs "$runs" \
  --limit "$limit" \
  --json "$report" \
  --compare-instruction \
  "${perf_args[@]}" \
  --simrv-arg=--ca
