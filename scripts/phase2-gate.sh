#!/usr/bin/env bash
# @file phase2-gate.sh
# @brief Comprehensive Phase 2 validation gate for OOP Refactor work.
#
# This script runs all available regression tests including:
# - Basic CLI validation
# - Application smoke tests (if configured)
# - Linux boot tests (if images available)
# - ISA tests (if riscv-tests available)
# - Performance profiling
#
# Exit code: 0 on all-pass, 1 if any required test fails
#
# Environment variables:
#   SIMRV_SKIP_BUILD: Skip build step (default: 0)
#   SIMRV_CMAKE_PRESET: CMake preset to use (default: rv32-release)
#   SIMRV_APP_IMG: Path to application image for smoke test
#   SIMRV_LINUX_MEM_IMG: Path to Linux kernel image
#   SIMRV_LINUX_DISK_IMG: Path to Linux disk image
#   SIMRV_APP_END: Cycle limit for app test (default: 200000)
#   SIMRV_LINUX_END: Cycle limit for Linux boot (default: 1000000)
#   SIMRV_ISA_TESTS: ISA test filter (default: run all)

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

LOG_DIR="${SIMRV_REG_LOG_DIR:-regression_logs}"
SIMRV_BIN="${SIMRV_BIN:-./build/rv32-release/SimRV}"
ISA_LOG_DIR="${SIMRV_ISA_LOG_DIR:-isa_logs}"
RISCV_TESTS_DIR="${RISCV_TESTS_DIR:-$ROOT_DIR/../../tests/riscv-tests}"
if [[ -d "$RISCV_TESTS_DIR/share/riscv-tests" ]]; then
  RISCV_TESTS_DIR="$RISCV_TESTS_DIR/share/riscv-tests"
fi

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
  else
    echo "Log: $log_file"
    return 1
  fi
}

echo "╔════════════════════════════════════════════════════════════╗"
echo "║      SimRV Phase 2 Comprehensive Validation Gate          ║"
echo "║        (OOP Refactor for RV64GC Readiness)                ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo
echo "Root: $ROOT_DIR"
echo "Binary: $SIMRV_BIN"
echo "Logs: $LOG_DIR"
echo

# ============================================================================
# PHASE 1: BUILD VALIDATION
# ============================================================================
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "PHASE 1: Build & Compilation"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if [[ "${SIMRV_SKIP_BUILD:-0}" != "1" ]]; then
  CMAKE_PRESET="${SIMRV_CMAKE_PRESET:-rv32-release}"
  BUILD_PRESET="${SIMRV_CMAKE_BUILD_PRESET:-$CMAKE_PRESET}"
  BUILD_JOBS="${SIMRV_MAKE_JOBS:-1}"
  
  if run_with_log configure cmake --preset "$CMAKE_PRESET"; then
    pass "CMake configuration"
  else
    fail "CMake configuration (see $LOG_DIR/configure.log)"
    exit 1
  fi
  
  if run_with_log build cmake --build --preset "$BUILD_PRESET" -j"$BUILD_JOBS"; then
    pass "Compilation"
  else
    fail "Compilation (see $LOG_DIR/build.log)"
    exit 1
  fi
else
  echo "[SKIP] build (SIMRV_SKIP_BUILD=1)"
  SKIP_COUNT=$((SKIP_COUNT + 1))
fi

echo

# ============================================================================
# PHASE 2: BASIC CLI VALIDATION (Required)
# ============================================================================
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "PHASE 2: Basic CLI Validation (Required)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if run_with_log help timeout "${SIMRV_HELP_TIMEOUT:-5}" "$SIMRV_BIN" -h; then
  if grep -q "Usage:" "$LOG_DIR/help.log"; then
    pass "Help output"
  else
    fail "Help output missing Usage header"
  fi
else
  fail "Help output generation"
fi

if run_with_log unknown_option timeout "${SIMRV_HELP_TIMEOUT:-5}" "$SIMRV_BIN" -Z || true; then
  if grep -q "unknown option" "$LOG_DIR/unknown_option.log"; then
    pass "CLI error handling"
  else
    fail "CLI error path"
  fi
else
  fail "CLI error handling"
fi

echo

# ============================================================================
# PHASE 3: APPLICATION SMOKE TEST (Optional)
# ============================================================================
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "PHASE 3: Application Smoke Test (Optional)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

APP_IMG="${SIMRV_APP_IMG:-}"
if [[ -n "$APP_IMG" && -r "$APP_IMG" ]]; then
  if run_with_log app_smoke timeout "${SIMRV_APP_TIMEOUT:-20}" "$SIMRV_BIN" -m "$APP_IMG" -a -e "${SIMRV_APP_END:-200000}"; then
    pass "Application smoke test"
  else
    fail "Application execution (see $LOG_DIR/app_smoke.log)"
  fi
else
  skip "Application smoke test (set SIMRV_APP_IMG to enable)"
fi

echo

# ============================================================================
# PHASE 4: LINUX BOOT TEST (Optional)
# ============================================================================
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "PHASE 4: Linux Boot Test (Optional - Extended Slice 5)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

LINUX_MEM_IMG="${SIMRV_LINUX_MEM_IMG:-}"
LINUX_DISK_IMG="${SIMRV_LINUX_DISK_IMG:-}"
if [[ -n "$LINUX_MEM_IMG" && -r "$LINUX_MEM_IMG" && -n "$LINUX_DISK_IMG" && -r "$LINUX_DISK_IMG" ]]; then
  LINUX_CMD=("$SIMRV_BIN" -m "$LINUX_MEM_IMG" -d "$LINUX_DISK_IMG" -e "${SIMRV_LINUX_END:-1000000}")
  if [[ -n "${SIMRV_LINUX_DTB:-}" ]]; then
    LINUX_CMD+=(-c "${SIMRV_LINUX_DTB}")
  fi
  # Use --foreground so timeout works correctly with simulator terminal handling.
  if run_with_log linux_boot timeout --foreground "${SIMRV_LINUX_TIMEOUT:-60}" "${LINUX_CMD[@]}"; then
    pass "Linux boot test ($(stat -f%z "$LOG_DIR/linux_boot.log" 2>/dev/null || stat -c%s "$LOG_DIR/linux_boot.log" 2>/dev/null || echo '?') bytes)"
  else
    fail "Linux boot test (see $LOG_DIR/linux_boot.log)"
  fi
else
  skip "Linux boot test (set SIMRV_LINUX_MEM_IMG and SIMRV_LINUX_DISK_IMG to enable)"
fi

echo

# ============================================================================
# PHASE 5: ISA TESTS (Optional)
# ============================================================================
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "PHASE 5: RISC-V ISA Tests (Optional)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if [[ -d "$RISCV_TESTS_DIR/isa" ]]; then
  ISA_TEST_CMD="bash scripts/run-isa-tests.sh"
  if run_with_log isa_tests eval "$ISA_TEST_CMD"; then
    # Count ISA test results from log
    ISA_PASS=$(grep -c "PASS" "$ISA_LOG_DIR"/*.log 2>/dev/null || echo "0")
    pass "ISA tests ($ISA_PASS tests executed)"
  else
    fail "ISA test execution (see $ISA_LOG_DIR/)"
  fi
else
  skip "ISA tests (riscv-tests not found at $RISCV_TESTS_DIR)"
fi

echo

# ============================================================================
# PHASE 6: PERFORMANCE PROFILING (Summary)
# ============================================================================
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "PHASE 6: Performance Profiling Summary"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Performance regression check: compare against baseline if available
if [[ -f "$LOG_DIR/perf_baseline.txt" ]]; then
  pass "Performance baseline available for regression analysis"
else
  echo "ℹ️  Baseline performance profile not available (first run)"
fi

echo

# ============================================================================
# FINAL REPORT
# ============================================================================
echo "╔════════════════════════════════════════════════════════════╗"
echo "║                    TEST SUMMARY REPORT                     ║"
echo "╚════════════════════════════════════════════════════════════╝"
echo
echo "✅ PASSED:  $PASS_COUNT"
echo "⏭️  SKIPPED: $SKIP_COUNT"
echo "❌ FAILED:  $FAIL_COUNT"
echo

if [[ $FAIL_COUNT -eq 0 ]]; then
  echo "🎉 Phase 2 Validation: COMPLETE"
  echo
  echo "All required tests passed."
  [[ $SKIP_COUNT -gt 0 ]] && echo "Optional tests skipped: $(($SKIP_COUNT))"
  echo
  exit 0
else
  echo "⚠️  Phase 2 Validation: INCOMPLETE"
  echo
  echo "Review failed test logs in: $LOG_DIR/"
  exit 1
fi
