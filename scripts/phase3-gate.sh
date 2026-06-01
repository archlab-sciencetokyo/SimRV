#!/usr/bin/env bash
# @file phase3-gate.sh
# @brief Comprehensive Phase 3 validation gate for XLEN abstraction work.

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

LOG_DIR="${SIMRV_PHASE3_LOG_DIR:-regression_logs}"
SIMRV_CMAKE_PRESET_RV32="${SIMRV_CMAKE_PRESET_RV32:-rv32-release}"
SIMRV_CMAKE_PRESET_RV64="${SIMRV_CMAKE_PRESET_RV64:-rv64-release}"
SIMRV_BUILD_PRESET_RV32="${SIMRV_CMAKE_BUILD_PRESET_RV32:-$SIMRV_CMAKE_PRESET_RV32}"
SIMRV_BUILD_PRESET_RV64="${SIMRV_CMAKE_BUILD_PRESET_RV64:-$SIMRV_CMAKE_PRESET_RV64}"
SIMRV_BIN_RV32="${SIMRV_BIN_RV32:-$ROOT_DIR/build/rv32-release/SimRV}"
SIMRV_BIN_RV64="${SIMRV_BIN_RV64:-$ROOT_DIR/build/rv64-release/SimRV}"

mkdir -p "$LOG_DIR"

PASS_COUNT=0
SKIP_COUNT=0
FAIL_COUNT=0

pass() {
  echo "✅ [PASS] $1"
  PASS_COUNT=$((PASS_COUNT + 1))
}

skip() {
  echo "⏭️  [SKIP] $1"
  SKIP_COUNT=$((SKIP_COUNT + 1))
}

fail() {
  echo "❌ [FAIL] $1"
  FAIL_COUNT=$((FAIL_COUNT + 1))
}

run_with_log() {
  local name="$1"
  shift
  local log_file="$LOG_DIR/$name.log"
  echo "[RUN ] $name..."
  if "$@" >"$log_file" 2>&1; then
    return 0
  fi
  echo "Log: $log_file"
  return 1
}

echo "╔════════════════════════════════════════════════════════════╗"
echo "║           SimRV Phase 3 Validation Gate                   ║"
echo "║             (RV32 + RV64 ISA Smoke Tests)                ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo
echo "Root: $ROOT_DIR"
echo "Logs: $LOG_DIR"
echo "RV32 preset: $SIMRV_CMAKE_PRESET_RV32"
echo "RV64 preset: $SIMRV_CMAKE_PRESET_RV64"
echo

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "PHASE 1: Build & Compilation"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if [[ "${SIMRV_SKIP_BUILD:-0}" != "1" ]]; then
  BUILD_JOBS="${SIMRV_MAKE_JOBS:-1}"

  if run_with_log configure_rv32 cmake --preset "$SIMRV_CMAKE_PRESET_RV32"; then
    pass "RV32 CMake configuration"
  else
    fail "RV32 CMake configuration"
    exit 1
  fi

  if run_with_log build_rv32 cmake --build --preset "$SIMRV_BUILD_PRESET_RV32" -j"$BUILD_JOBS"; then
    pass "RV32 compilation"
  else
    fail "RV32 compilation"
    exit 1
  fi

  if run_with_log configure_rv64 cmake --preset "$SIMRV_CMAKE_PRESET_RV64"; then
    pass "RV64 CMake configuration"
  else
    fail "RV64 CMake configuration"
    exit 1
  fi

  if run_with_log build_rv64 cmake --build --preset "$SIMRV_BUILD_PRESET_RV64" -j"$BUILD_JOBS"; then
    pass "RV64 compilation"
  else
    fail "RV64 compilation"
    exit 1
  fi
else
  skip "build (SIMRV_SKIP_BUILD=1)"
fi

echo
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "PHASE 2: RV32 ISA Smoke Tests"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if [[ -x "$SIMRV_BIN_RV32" ]]; then
  if run_with_log isa_rv32 env SIMRV_SKIP_BUILD=1 SIMRV_BIN="$SIMRV_BIN_RV32" SIMRV_ISA_XLEN=32 bash scripts/run-isa-tests.sh; then
    pass "RV32 ISA smoke tests"
  else
    fail "RV32 ISA smoke tests"
  fi
else
  skip "RV32 ISA smoke tests (missing binary: $SIMRV_BIN_RV32)"
fi

echo
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "PHASE 3: RV64 ISA Smoke Tests"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if [[ -x "$SIMRV_BIN_RV64" ]]; then
  if run_with_log isa_rv64 env SIMRV_SKIP_BUILD=1 SIMRV_BIN="$SIMRV_BIN_RV64" SIMRV_ISA_XLEN=64 bash scripts/run-isa-tests.sh; then
    pass "RV64 ISA smoke tests"
  else
    fail "RV64 ISA smoke tests"
  fi
else
  skip "RV64 ISA smoke tests (missing binary: $SIMRV_BIN_RV64)"
fi

echo
echo "╔════════════════════════════════════════════════════════════╗"
echo "║                    TEST SUMMARY REPORT                     ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo
echo "✅ PASSED:  $PASS_COUNT"
echo "⏭️  SKIPPED: $SKIP_COUNT"
echo "❌ FAILED:  $FAIL_COUNT"
echo

if [[ $FAIL_COUNT -eq 0 ]]; then
  echo "🎉 Phase 3 Validation: COMPLETE"
  exit 0
fi

echo "⚠️  Phase 3 Validation: INCOMPLETE"
exit 1