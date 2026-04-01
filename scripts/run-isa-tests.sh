#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

DEFAULT_RISCV_TESTS_DIR="$ROOT_DIR/../../tests/riscv-tests"
RISCV_TESTS_DIR="${RISCV_TESTS_DIR:-$DEFAULT_RISCV_TESTS_DIR}"
if [[ ! -d "$RISCV_TESTS_DIR" ]]; then
  echo "ERROR: riscv-tests directory not found: $RISCV_TESTS_DIR"
  echo "Set RISCV_TESTS_DIR to your checkout/build directory"
  echo "Example: RISCV_TESTS_DIR=$HOME/riscv-tests make isa-tests"
  exit 2
fi

if [[ -z "${RISCV_PREFIX:-}" ]]; then
  if command -v riscv64-unknown-elf-gcc >/dev/null 2>&1; then
    RISCV_PREFIX="riscv64-unknown-elf-"
  elif [[ -x "/var/archlab-modules/riscv/2026.03.13/bin/riscv64-unknown-elf-gcc" ]]; then
    RISCV_PREFIX="/var/archlab-modules/riscv/2026.03.13/bin/riscv64-unknown-elf-"
  else
    RISCV_PREFIX=""
  fi
fi

if [[ -z "${RISCV_OBJCOPY:-}" ]]; then
  if [[ -n "$RISCV_PREFIX" && -x "${RISCV_PREFIX}objcopy" ]]; then
    OBJCOPY="${RISCV_PREFIX}objcopy"
  elif command -v riscv64-unknown-elf-objcopy >/dev/null 2>&1; then
    OBJCOPY="riscv64-unknown-elf-objcopy"
  else
    OBJCOPY="objcopy"
  fi
else
  OBJCOPY="$RISCV_OBJCOPY"
fi

if ! command -v "$OBJCOPY" >/dev/null 2>&1; then
  echo "ERROR: objcopy tool not found: $OBJCOPY"
  exit 2
fi

TIMEOUT_SECS="${SIMRV_ISA_TIMEOUT:-20}"
END_INSNS="${SIMRV_ISA_END:-2000000}"
TOHOST_ADDR="${SIMRV_ISA_TOHOST:-0x80001000}"
LOG_DIR="${SIMRV_ISA_LOG_DIR:-isa_logs}"
WORK_DIR="${SIMRV_ISA_WORK_DIR:-.isa_tmp}"
SIMRV_BIN="${SIMRV_BIN:-./simrv}"

mkdir -p "$LOG_DIR" "$WORK_DIR"

echo "== SimRV riscv-isa-tests runner =="
echo "RISCV_TESTS_DIR=$RISCV_TESTS_DIR"
echo "OBJCOPY=$OBJCOPY"
echo "RISCV_PREFIX=${RISCV_PREFIX:-<unset>}"
echo "TIMEOUT=$TIMEOUT_SECS END=$END_INSNS TOHOST=$TOHOST_ADDR"

if [[ "${SIMRV_SKIP_BUILD:-0}" != "1" ]]; then
  CMAKE_PRESET="${SIMRV_CMAKE_PRESET:-ninja-clang-release}"
  BUILD_PRESET="${SIMRV_CMAKE_BUILD_PRESET:-$CMAKE_PRESET}"
  BUILD_JOBS="${SIMRV_MAKE_JOBS:-1}"
  cmake --preset "$CMAKE_PRESET" >/dev/null
  cmake --build --preset "$BUILD_PRESET" -j"$BUILD_JOBS" >/dev/null
fi

ISA_DIR="$RISCV_TESTS_DIR/isa"
if [[ ! -d "$ISA_DIR" ]]; then
  echo "ERROR: cannot find isa directory under $RISCV_TESTS_DIR"
  exit 2
fi

if [[ -n "${SIMRV_ISA_LIST:-}" ]]; then
  mapfile -t TESTS < <(printf '%s\n' "$SIMRV_ISA_LIST" | tr ',' '\n' | sed '/^$/d')
else
  TESTS=(
    rv32ui-p-add
    rv32ui-p-addi
    rv32ui-p-and
    rv32ui-p-beq
    rv32ui-p-bne
    rv32ui-p-lw
    rv32ui-p-sw
  )
fi

PASS=0
FAIL=0
SKIP=0

for t in "${TESTS[@]}"; do
  ELF="$ISA_DIR/$t"
  if [[ ! -f "$ELF" ]]; then
    if [[ -n "$RISCV_PREFIX" ]]; then
      echo "[MAKE] $t"
      if ! make -C "$ISA_DIR" -f ../isa/Makefile src_dir=../isa XLEN=32 RISCV_PREFIX="$RISCV_PREFIX" "$t" >/dev/null 2>&1; then
        echo "[SKIP] $t (build failed in riscv-tests/isa)"
        SKIP=$((SKIP + 1))
        continue
      fi
    else
      echo "[SKIP] $t (missing $ELF and RISCV_PREFIX is unavailable for auto-build)"
      SKIP=$((SKIP + 1))
      continue
    fi
  fi

  BIN="$WORK_DIR/$t.bin"
  LOG="$LOG_DIR/$t.log"

  if ! "$OBJCOPY" -O binary "$ELF" "$BIN" >/dev/null 2>&1; then
    echo "[SKIP] $t (objcopy failed)"
    SKIP=$((SKIP + 1))
    continue
  fi

  echo "[RUN ] $t"
  if timeout "$TIMEOUT_SECS" "$SIMRV_BIN" -m "$BIN" -e "$END_INSNS" -T -H "$TOHOST_ADDR" </dev/null >"$LOG" 2>&1; then
    if grep -q "ISA TEST PASS" "$LOG"; then
      echo "[PASS] $t"
      PASS=$((PASS + 1))
    elif grep -q "ISA TEST FAIL" "$LOG"; then
      echo "[FAIL] $t"
      FAIL=$((FAIL + 1))
    else
      echo "[FAIL] $t (no ISA pass/fail marker)"
      FAIL=$((FAIL + 1))
    fi
  else
    echo "[FAIL] $t (simulator error/timeout)"
    FAIL=$((FAIL + 1))
  fi
done

echo
echo "Summary: PASS=$PASS FAIL=$FAIL SKIP=$SKIP"
if [[ "$FAIL" -ne 0 ]]; then
  exit 1
fi
