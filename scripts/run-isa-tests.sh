#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

DEFAULT_RISCV_TESTS_DIR="$ROOT_DIR/../../tests/riscv-tests"
RISCV_TESTS_DIR="${RISCV_TESTS_DIR:-$DEFAULT_RISCV_TESTS_DIR}"
if [[ -d "$RISCV_TESTS_DIR/share/riscv-tests" ]]; then
  RISCV_TESTS_DIR="$RISCV_TESTS_DIR/share/riscv-tests"
fi
if [[ ! -d "$RISCV_TESTS_DIR" ]]; then
  echo "ERROR: riscv-tests directory not found: $RISCV_TESTS_DIR"
  echo "Set RISCV_TESTS_DIR to your checkout/build directory"
  echo "Example: RISCV_TESTS_DIR=$HOME/riscv-tests ctest --test-dir build/rv32-release -L gate"
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

if [[ -z "${RISCV_NM:-}" ]]; then
  if [[ -n "$RISCV_PREFIX" && -x "${RISCV_PREFIX}nm" ]]; then
    NM_TOOL="${RISCV_PREFIX}nm"
  elif command -v riscv64-unknown-elf-nm >/dev/null 2>&1; then
    NM_TOOL="riscv64-unknown-elf-nm"
  elif command -v nm >/dev/null 2>&1; then
    NM_TOOL="nm"
  else
    NM_TOOL=""
  fi
else
  NM_TOOL="$RISCV_NM"
fi

TIMEOUT_SECS="${SIMRV_ISA_TIMEOUT:-20}"
END_INSNS="${SIMRV_ISA_END:-2000000}"
TOHOST_ADDR="${SIMRV_ISA_TOHOST:-0x80001000}"
LOG_DIR="${SIMRV_ISA_LOG_DIR:-isa_logs}"
WORK_DIR="${SIMRV_ISA_WORK_DIR:-.isa_tmp}"
SIMRV_BIN="${SIMRV_BIN:-./build/rv32-release/SimRV}"
ISA_XLEN="${SIMRV_ISA_XLEN:-32}"

if [[ "$ISA_XLEN" != "32" && "$ISA_XLEN" != "64" ]]; then
  echo "ERROR: SIMRV_ISA_XLEN must be 32 or 64 (got: $ISA_XLEN)"
  exit 2
fi

mkdir -p "$LOG_DIR" "$WORK_DIR"

echo "== SimRV riscv-isa-tests runner =="
echo "RISCV_TESTS_DIR=$RISCV_TESTS_DIR"
echo "OBJCOPY=$OBJCOPY"
echo "RISCV_PREFIX=${RISCV_PREFIX:-<unset>}"
echo "XLEN=$ISA_XLEN TIMEOUT=$TIMEOUT_SECS END=$END_INSNS TOHOST=$TOHOST_ADDR"

if [[ "${SIMRV_SKIP_BUILD:-0}" != "1" ]]; then
  CMAKE_PRESET="${SIMRV_CMAKE_PRESET:-rv32-release}"
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
  if [[ "$ISA_XLEN" == "64" ]]; then
    TESTS=(
      rv64ui-p-add
      rv64ui-p-addi
      rv64ui-p-and
      rv64ui-p-auipc
      rv64ui-p-beq
      rv64ui-p-bge
      rv64ui-p-bgeu
      rv64ui-p-blt
      rv64ui-p-bltu
      rv64ui-p-bne
      rv64ui-p-jal
      rv64ui-p-jalr
      rv64ui-p-lb
      rv64ui-p-lbu
      rv64ui-p-lh
      rv64ui-p-lhu
      rv64ui-p-lw
      rv64ui-p-lwu
      rv64ui-p-or
      rv64ui-p-sb
      rv64ui-p-sd
      rv64ui-p-sh
      rv64ui-p-sw
      rv64ui-p-sll
      rv64ui-p-slli
      rv64ui-p-slt
      rv64ui-p-sltu
      rv64ui-p-sra
      rv64ui-p-srai
      rv64ui-p-srl
      rv64ui-p-srli
      rv64ui-p-sub
      rv64ui-p-xor
      rv64um-p-mul
      rv64um-p-mulh
      rv64um-p-mulhsu
      rv64um-p-mulhu
      rv64um-p-div
      rv64um-p-divu
      rv64um-p-rem
      rv64um-p-remu
      rv64ua-p-amoadd_w
      rv64ua-p-amomax_w
      rv64ua-p-amomaxu_w
      rv64ua-p-amomin_w
      rv64ua-p-amominu_w
      rv64ua-p-amoor_w
      rv64ua-p-amoswap_w
      rv64ua-p-amoxor_w
      rv64ua-p-lrsc
    )
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
fi

PASS=0
FAIL=0
SKIP=0

for t in "${TESTS[@]}"; do
  ELF="$ISA_DIR/$t"
  if [[ ! -f "$ELF" ]]; then
    if [[ -n "$RISCV_PREFIX" ]]; then
      echo "[MAKE] $t"
      if ! make -C "$ISA_DIR" -f ../isa/Makefile src_dir=../isa XLEN="$ISA_XLEN" RISCV_PREFIX="$RISCV_PREFIX" "$t" >/dev/null 2>&1; then
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
  TEST_TOHOST_ADDR="$TOHOST_ADDR"
  if [[ -n "$NM_TOOL" ]]; then
    TOHOST_SYM="$($NM_TOOL -g "$ELF" 2>/dev/null | awk '$3=="tohost" {print $1; exit}')"
    if [[ -n "$TOHOST_SYM" ]]; then
      TEST_TOHOST_ADDR="0x${TOHOST_SYM}"
    fi
  fi

  if timeout "$TIMEOUT_SECS" "$SIMRV_BIN" -m "$BIN" -e "$END_INSNS" -T -H "$TEST_TOHOST_ADDR" </dev/null >"$LOG" 2>&1; then
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
