#!/usr/bin/env bash
# @file benchmark.sh
# @brief Unified benchmark runner for SimRV instruction and cycle-accurate execution.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

SIMRV_BIN="${SIMRV_BIN:-./build/rv64-release/SimRV}"
if [[ ! -x "$SIMRV_BIN" && -x "./build/rv32-release/SimRV" ]]; then
  SIMRV_BIN="./build/rv32-release/SimRV"
fi

BENCH_ITERS="${SIMRV_BENCH_ITERS:-5}"
BENCH_LIMIT="${SIMRV_BENCH_LIMIT:-20000000}"
BENCH_TIMEOUT="${SIMRV_BENCH_TIMEOUT:-30}"
BENCH_TEST_NAME="${SIMRV_BENCH_TEST:-dhrystone}"
BENCH_TOHOST_ADDR="${SIMRV_BENCH_TOHOST:-}"
RISCV_TESTS_DIR="${RISCV_TESTS_DIR:-}"
AFFINITY="${SIMRV_CA_BENCH_AFFINITY:-}"

TEST_ARG="${SIMRV_BENCH_IMG:-$BENCH_TEST_NAME}"

extra_args=()
runner=()

# Process flags recognized directly by this wrapper
while [[ $# -gt 0 ]]; do
  case "$1" in
    --ca|--cycle-accurate)
      extra_args+=(--simrv-arg=--mode --simrv-arg=cycle-accurate --compare-instruction)
      shift
      ;;
    --affinity)
      AFFINITY="$2"
      shift 2
      ;;
    -h|--help)
      echo "Usage: $0 [options] [-- any benchmark.py options]"
      echo "Wrapper for scripts/benchmark.py with sensible defaults and environment overrides."
      echo ""
      echo "Options:"
      echo "  --ca, --cycle-accurate     Run in cycle-accurate microarchitecture mode"
      echo "  --affinity <cores>         Bind execution to specified CPU cores (requires taskset)"
      echo "  -h, --help                 Show this help message"
      echo ""
      echo "Environment Overrides:"
      echo "  SIMRV_BIN                  Path to SimRV executable (default: auto-detected)"
      echo "  SIMRV_BENCH_TEST           Benchmark test name/path (default: dhrystone)"
      echo "  SIMRV_BENCH_ITERS          Number of runs (default: 5)"
      echo "  SIMRV_BENCH_LIMIT          Instruction limit (default: 20000000)"
      echo "  SIMRV_BENCH_TIMEOUT        Timeout in seconds (default: 30)"
      echo "  SIMRV_CA_BENCH_PERF=1      Enable host Linux perf hardware counters"
      exit 0
      ;;
    *)
      extra_args+=("$1")
      shift
      ;;
  esac
done

if [[ "${SIMRV_CA_BENCH_PERF:-0}" == "1" ]]; then
  extra_args+=(--perf)
fi

if [[ -n "$AFFINITY" ]]; then
  if ! command -v taskset >/dev/null 2>&1; then
    echo "Warning: taskset not available; ignoring affinity $AFFINITY" >&2
  else
    runner=(taskset -c "$AFFINITY")
  fi
fi

exec "${runner[@]}" python3 "$ROOT_DIR/scripts/benchmark.py" \
  --simrv "$SIMRV_BIN" \
  --runs "$BENCH_ITERS" \
  --limit "$BENCH_LIMIT" \
  --timeout "$BENCH_TIMEOUT" \
  --test "$TEST_ARG" \
  ${BENCH_TOHOST_ADDR:+--tohost "$BENCH_TOHOST_ADDR"} \
  ${RISCV_TESTS_DIR:+--riscv-tests-dir "$RISCV_TESTS_DIR"} \
  "${extra_args[@]}"
