#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

LOG_DIR="${SIMRV_REG_LOG_DIR:-regression_logs}"
SIMRV_BIN="${SIMRV_BIN:-./simrv}"
mkdir -p "$LOG_DIR"

PASS_COUNT=0
SKIP_COUNT=0

pass() {
  echo "[PASS] $1"
  PASS_COUNT=$((PASS_COUNT + 1))
}

skip() {
  echo "[SKIP] $1"
  SKIP_COUNT=$((SKIP_COUNT + 1))
}

run_with_log() {
  local name="$1"
  shift
  local log_file="$LOG_DIR/$name.log"
  echo "[RUN ] $name"
  "$@" >"$log_file" 2>&1
}

echo "== SimRV Regression (Phase 0 baseline) =="
echo "Root: $ROOT_DIR"
echo "Logs: $LOG_DIR"

if [[ "${SIMRV_SKIP_BUILD:-0}" != "1" ]]; then
  CMAKE_PRESET="${SIMRV_CMAKE_PRESET:-ninja-clang-release}"
  BUILD_PRESET="${SIMRV_CMAKE_BUILD_PRESET:-$CMAKE_PRESET}"
  BUILD_JOBS="${SIMRV_MAKE_JOBS:-1}"
  run_with_log configure cmake --preset "$CMAKE_PRESET"
  run_with_log build cmake --build --preset "$BUILD_PRESET" -j"$BUILD_JOBS"
  pass "build"
else
  echo "[SKIP] build (SIMRV_SKIP_BUILD=1)"
  SKIP_COUNT=$((SKIP_COUNT + 1))
fi

run_with_log help timeout "${SIMRV_HELP_TIMEOUT:-5}" "$SIMRV_BIN" -h
if grep -q "Usage:" "$LOG_DIR/help.log"; then
  pass "help output"
else
  echo "[FAIL] help output missing expected usage header"
  echo "See $LOG_DIR/help.log"
  exit 1
fi

run_with_log unknown_option timeout "${SIMRV_HELP_TIMEOUT:-5}" "$SIMRV_BIN" -Z || true
if grep -q "unknown option" "$LOG_DIR/unknown_option.log"; then
  pass "CLI error path"
else
  echo "[FAIL] unknown option path did not emit expected error"
  echo "See $LOG_DIR/unknown_option.log"
  exit 1
fi

APP_IMG="${SIMRV_APP_IMG:-}"
if [[ -n "$APP_IMG" && -r "$APP_IMG" ]]; then
  run_with_log app_smoke timeout "${SIMRV_APP_TIMEOUT:-20}" "$SIMRV_BIN" -m "$APP_IMG" -a -e "${SIMRV_APP_END:-200000}"
  pass "app smoke"
else
  skip "app smoke (set readable SIMRV_APP_IMG to enable)"
fi

LINUX_MEM_IMG="${SIMRV_LINUX_MEM_IMG:-}"
LINUX_DISK_IMG="${SIMRV_LINUX_DISK_IMG:-}"
if [[ -n "$LINUX_MEM_IMG" && -r "$LINUX_MEM_IMG" && -n "$LINUX_DISK_IMG" && -r "$LINUX_DISK_IMG" ]]; then
  LINUX_CMD=("$SIMRV_BIN" -m "$LINUX_MEM_IMG" -d "$LINUX_DISK_IMG" -e "${SIMRV_LINUX_END:-1000000}")
  if [[ -n "${SIMRV_LINUX_DTB:-}" ]]; then
    LINUX_CMD+=( -c "${SIMRV_LINUX_DTB}" )
  fi
  run_with_log linux_smoke timeout "${SIMRV_LINUX_TIMEOUT:-45}" "${LINUX_CMD[@]}"
  pass "linux smoke"
else
  skip "linux smoke (set readable SIMRV_LINUX_MEM_IMG and SIMRV_LINUX_DISK_IMG to enable)"
fi

echo
echo "Summary: PASS=$PASS_COUNT SKIP=$SKIP_COUNT"
echo "Regression baseline complete."
