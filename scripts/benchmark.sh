#!/usr/bin/env bash
# @file benchmark.sh
# @brief Unified benchmark runner for SimRV instruction and cycle-accurate execution.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

SIMRV_BENCH_BIN="${SIMRV_BENCH_BIN:-./build/rv64-release/simrv-benchmark}"
if [[ ! -x "$SIMRV_BENCH_BIN" && -x "./build/rv32-release/simrv-benchmark" ]]; then
  SIMRV_BENCH_BIN="./build/rv32-release/simrv-benchmark"
fi

BENCH_ITERS="${SIMRV_BENCH_ITERS:-5}"
BENCH_TEST_NAME="${SIMRV_BENCH_TEST:-dhrystone}"
AFFINITY="${SIMRV_CA_BENCH_AFFINITY:-}"

TEST_ARG="${SIMRV_BENCH_IMG:-$BENCH_TEST_NAME}"

extra_args=()
runner=()

# Process flags recognized directly by this wrapper
while [[ $# -gt 0 ]]; do
  case "$1" in
    --ca|--cycle-accurate)
      extra_args+=(--ca)
      shift
      ;;
    --affinity)
      AFFINITY="$2"
      shift 2
      ;;
    -h|--help)
      echo "Usage: $0 [options] [ELF]"
      echo "Wrapper for native simrv-benchmark with sensible defaults and environment overrides."
      echo ""
      echo "Options:"
      echo "  --ca, --cycle-accurate     Run in cycle-accurate microarchitecture mode"
      echo "  --affinity <cores>         Bind execution to specified CPU cores (requires taskset)"
      echo "  -h, --help                 Show this help message"
      echo ""
      echo "Environment Overrides:"
      echo "  SIMRV_BENCH_BIN            Path to simrv-benchmark executable"
      echo "  SIMRV_BENCH_TEST           Benchmark test name/path (default: dhrystone)"
      echo "  SIMRV_BENCH_ITERS          Number of runs (default: 5)"
      echo "  SIMRV_CA_BENCH_AFFINITY    CPU core pinning for taskset"
      exit 0
      ;;
    *)
      extra_args+=("$1")
      shift
      ;;
  esac
done

if [[ -n "$AFFINITY" ]]; then
  if ! command -v taskset >/dev/null 2>&1; then
    echo "Warning: taskset not available; ignoring affinity $AFFINITY" >&2
  else
    runner=(taskset -c "$AFFINITY")
  fi
fi

exec "${runner[@]}" "$SIMRV_BENCH_BIN" \
  --runs "$BENCH_ITERS" \
  ${extra_args[@]+"${extra_args[@]}"} \
  "$TEST_ARG"
