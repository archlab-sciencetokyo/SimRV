#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

SIMRV_BIN="${SIMRV_BIN:-./build/ninja-clang-release/SimRV}"
BENCH_LOG_DIR="${SIMRV_BENCH_LOG_DIR:-benchmark_logs}"
BENCH_WORK_DIR="${SIMRV_BENCH_WORK_DIR:-.bench_tmp}"
BENCH_ITERS="${SIMRV_BENCH_ITERS:-5}"
BENCH_END="${SIMRV_BENCH_END:-2000000}"
BENCH_TIMEOUT="${SIMRV_BENCH_TIMEOUT:-20}"
BENCH_IMG="${SIMRV_BENCH_IMG:-}"
BENCH_TEST_NAME="${SIMRV_BENCH_TEST:-rv32ui-p-add}"
BENCH_TOHOST_ADDR="${SIMRV_BENCH_TOHOST:-0x80001000}"

DEFAULT_RISCV_TESTS_DIR="$ROOT_DIR/../../tests/riscv-tests"
RISCV_TESTS_DIR="${RISCV_TESTS_DIR:-$DEFAULT_RISCV_TESTS_DIR}"

mkdir -p "$BENCH_LOG_DIR" "$BENCH_WORK_DIR"

if [[ ! "$BENCH_ITERS" =~ ^[0-9]+$ ]] || [[ "$BENCH_ITERS" -le 0 ]]; then
  echo "ERROR: SIMRV_BENCH_ITERS must be a positive integer"
  exit 1
fi

if [[ -z "$BENCH_IMG" ]]; then
  ELF="$RISCV_TESTS_DIR/isa/$BENCH_TEST_NAME"
  if [[ ! -f "$ELF" ]]; then
    echo "SKIP: benchmark image not found and riscv-tests ELF is unavailable: $ELF"
    echo "Set SIMRV_BENCH_IMG to a readable binary image, or provide RISCV_TESTS_DIR with prebuilt tests"
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

  if [[ -n "${RISCV_OBJCOPY:-}" ]]; then
    OBJCOPY="$RISCV_OBJCOPY"
  elif [[ -n "$RISCV_PREFIX" && -x "${RISCV_PREFIX}objcopy" ]]; then
    OBJCOPY="${RISCV_PREFIX}objcopy"
  elif command -v riscv64-unknown-elf-objcopy >/dev/null 2>&1; then
    OBJCOPY="riscv64-unknown-elf-objcopy"
  elif command -v objcopy >/dev/null 2>&1; then
    OBJCOPY="objcopy"
  else
    echo "SKIP: no objcopy found (set RISCV_OBJCOPY)"
    exit 2
  fi

  if [[ -n "${RISCV_NM:-}" ]]; then
    NM_TOOL="$RISCV_NM"
  elif [[ -n "$RISCV_PREFIX" && -x "${RISCV_PREFIX}nm" ]]; then
    NM_TOOL="${RISCV_PREFIX}nm"
  elif command -v riscv64-unknown-elf-nm >/dev/null 2>&1; then
    NM_TOOL="riscv64-unknown-elf-nm"
  elif command -v nm >/dev/null 2>&1; then
    NM_TOOL="nm"
  else
    NM_TOOL=""
  fi

  BENCH_IMG="$BENCH_WORK_DIR/$BENCH_TEST_NAME.bin"
  "$OBJCOPY" -O binary "$ELF" "$BENCH_IMG"

  if [[ -n "$NM_TOOL" ]]; then
    TOHOST_SYM="$($NM_TOOL -g "$ELF" 2>/dev/null | awk '$3=="tohost" {print $1; exit}')"
    if [[ -n "$TOHOST_SYM" ]]; then
      BENCH_TOHOST_ADDR="0x${TOHOST_SYM}"
    fi
  fi
fi

if [[ ! -r "$BENCH_IMG" ]]; then
  echo "ERROR: benchmark image is not readable: $BENCH_IMG"
  exit 1
fi

echo "== SimRV benchmark =="
echo "SIMRV_BIN=$SIMRV_BIN"
echo "IMAGE=$BENCH_IMG"
echo "ITERS=$BENCH_ITERS END=$BENCH_END TIMEOUT=$BENCH_TIMEOUT"
echo "TOHOST=$BENCH_TOHOST_ADDR"

times_file="$BENCH_WORK_DIR/bench_kips.txt"
: > "$times_file"

for i in $(seq 1 "$BENCH_ITERS"); do
  log_file="$BENCH_LOG_DIR/bench_$i.log"
  echo "[RUN ] iter=$i"
  if ! timeout "$BENCH_TIMEOUT" "$SIMRV_BIN" -m "$BENCH_IMG" -e "$BENCH_END" -T -H "$BENCH_TOHOST_ADDR" </dev/null >"$log_file" 2>&1; then
    echo "ERROR: benchmark iteration failed or timed out (iter=$i)"
    echo "See $log_file"
    exit 1
  fi

  kips="$(awk '/Simulation speed \(KIPS\)/ {print $NF; exit}' "$log_file")"
  if [[ -z "$kips" || ! "$kips" =~ ^[0-9]+$ ]]; then
    echo "ERROR: could not parse Simulation speed (KIPS) from $log_file"
    exit 1
  fi
  echo "$kips" >> "$times_file"
done

mean_kips="$(awk '{sum+=$1} END {if (NR>0) printf "%.2f", sum/NR; else print "0"}' "$times_file")"
median_kips="$(sort -n "$times_file" | awk '
  {a[NR]=$1}
  END {
    if (NR == 0) { print "0"; exit }
    if (NR % 2 == 1) {
      print a[(NR+1)/2]
    } else {
      printf "%.2f", (a[NR/2] + a[NR/2 + 1]) / 2.0
    }
  }
')"
min_kips="$(sort -n "$times_file" | head -n 1)"
max_kips="$(sort -n "$times_file" | tail -n 1)"

echo
echo "Benchmark Summary"
echo "  mean KIPS   : $mean_kips"
echo "  median KIPS : $median_kips"
echo "  min KIPS    : $min_kips"
echo "  max KIPS    : $max_kips"
echo "  logs        : $BENCH_LOG_DIR"
