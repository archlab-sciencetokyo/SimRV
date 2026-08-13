#!/usr/bin/env bash
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

# Fallback to SIMRV_BENCH_IMG if specified
TEST_ARG="$BENCH_TEST_NAME"
if [[ -n "${SIMRV_BENCH_IMG:-}" ]]; then
  TEST_ARG="$SIMRV_BENCH_IMG"
fi

exec python3 "$ROOT_DIR/scripts/benchmark.py" \
  --simrv "$SIMRV_BIN" \
  --runs "$BENCH_ITERS" \
  --limit "$BENCH_LIMIT" \
  --timeout "$BENCH_TIMEOUT" \
  --test "$TEST_ARG" \
  ${BENCH_TOHOST_ADDR:+--tohost "$BENCH_TOHOST_ADDR"} \
  ${RISCV_TESTS_DIR:+--riscv-tests-dir "$RISCV_TESTS_DIR"} \
  "$@"
