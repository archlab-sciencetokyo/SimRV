#!/usr/bin/env python3
"""
@file compare_arch.py
@brief Compare 32-bit vs 64-bit execution metrics (Instructions, Wall Time, MIPS, Spike speedup).
"""

import argparse
import json
import os
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))


def run_benchmark(simrv_bin, spike_bin, elf_path, isa, runs, limit, timeout):
    benchmark_py = os.path.join(SCRIPT_DIR, "benchmark.py")
    tmp_json = os.path.join(ROOT_DIR, ".bench_tmp", f"arch_{os.path.basename(elf_path)}.json")
    os.makedirs(os.path.dirname(tmp_json), exist_ok=True)

    cmd = [
        sys.executable,
        benchmark_py,
        "--simrv", simrv_bin,
        "--test", elf_path,
        "--runs", str(runs),
        "--limit", str(limit),
        "--timeout", str(timeout),
        "--isa", isa,
        "--json", tmp_json,
    ]
    if spike_bin:
        cmd.extend(["--spike", spike_bin])

    subprocess.run(cmd, check=True)
    with open(tmp_json, "r") as f:
        return json.load(f)


def main():
    parser = argparse.ArgumentParser(description="32-bit vs 64-bit RISC-V Benchmark Comparison")
    parser.add_argument("--benchmark", default="aha-mont", help="Benchmark name (e.g. aha-mont)")
    parser.add_argument("--simrv64", default="./build/rv64-release/SimRV", help="Path to RV64 SimRV binary")
    parser.add_argument("--simrv32", default="./build/rv32-release/SimRV", help="Path to RV32 SimRV binary")
    parser.add_argument("--spike", default="spike", help="Path to Spike binary (or empty to disable)")
    parser.add_argument("-n", "--runs", type=int, default=5, help="Number of benchmark iterations")
    parser.add_argument("-e", "--limit", type=int, default=50000000, help="Instruction limit")
    parser.add_argument("--timeout", type=int, default=60, help="Timeout in seconds")
    parser.add_argument("--json", help="Export JSON summary report")

    args = parser.parse_args()

    bench_name = args.benchmark
    tests_dir = os.environ.get("TESTS_DIR", os.path.abspath(os.path.join(SCRIPT_DIR, "../../../tests")))

    candidates_64 = [
        os.path.join(tests_dir, "riscv-tests/benchmarks", f"{bench_name}64.riscv"),
        os.path.join(tests_dir, "riscv-tests/benchmarks", f"{bench_name}.riscv"),
        os.path.join(tests_dir, "benchmarks", f"{bench_name}64.riscv"),
        os.path.join(tests_dir, "benchmarks", f"{bench_name}.riscv"),
        os.path.join(tests_dir, "coremark", f"{bench_name}64.riscv"),
        os.path.join(tests_dir, "coremark", f"{bench_name}.riscv"),
        os.path.join(ROOT_DIR, f"benchmarks/bin/{bench_name}64.riscv"),
        os.path.join(ROOT_DIR, f"benchmarks/bin/{bench_name}.riscv"),
    ]
    candidates_32 = [
        os.path.join(tests_dir, "riscv-tests/benchmarks", f"{bench_name}32.riscv"),
        os.path.join(tests_dir, "riscv-tests/benchmarks", f"{bench_name}.riscv"),
        os.path.join(tests_dir, "benchmarks", f"{bench_name}32.riscv"),
        os.path.join(tests_dir, "benchmarks", f"{bench_name}.riscv"),
        os.path.join(tests_dir, "coremark", f"{bench_name}32.riscv"),
        os.path.join(tests_dir, "coremark", f"{bench_name}.riscv"),
        os.path.join(ROOT_DIR, f"benchmarks/bin/{bench_name}32.riscv"),
        os.path.join(ROOT_DIR, f"benchmarks/bin/{bench_name}.riscv"),
    ]

    elf64 = next((c for c in candidates_64 if os.path.isfile(c)), None)
    elf32 = next((c for c in candidates_32 if os.path.isfile(c)), None)

    if not elf64:
        print(f"Error: 64-bit ELF not found for {bench_name}", file=sys.stderr)
        sys.exit(1)

    if not elf32:
        print(f"Error: 32-bit ELF not found for {bench_name}", file=sys.stderr)
        sys.exit(1)

    print(f"\n====================================================================================")
    print(f" 32-bit vs 64-bit Architectural Benchmark Comparison: {bench_name}")
    print(f"====================================================================================")

    res64 = run_benchmark(args.simrv64, args.spike, elf64, "rv64gc", args.runs, args.limit, args.timeout)
    res32 = run_benchmark(args.simrv32, args.spike, elf32, "rv32imac", args.runs, args.limit, args.timeout)

    inst64 = res64.get("instructions", 0)
    inst32 = res32.get("instructions", 0)
    t64 = res64["simrv"]["stats"]["time"]["mean"]
    t32 = res32["simrv"]["stats"]["time"]["mean"]
    mips64 = res64["simrv"]["stats"]["wall_speed"]["mean"] / 1000.0
    mips32 = res32["simrv"]["stats"]["wall_speed"]["mean"] / 1000.0

    inst_ratio = inst32 / inst64 if inst64 > 0 else 0.0
    time_ratio = t32 / t64 if t64 > 0 else 0.0

    print("\n" + "=" * 90)
    print(f"{'ARCHITECTURAL COMPARISON SUMMARY: ' + bench_name.upper():^90}")
    print("=" * 90)
    print(f"{'Metric':<30} | {'RV32':>15} | {'RV64':>15} | {'RV32 / RV64 Ratio':>20}")
    print("-" * 90)
    print(f"{'Instructions':<30} | {inst32:>15,} | {inst64:>15,} | {inst_ratio:>19.2f}x")
    print(f"{'SimRV Wall Time (s)':<30} | {t32:>15.4f} | {t64:>15.4f} | {time_ratio:>19.2f}x")
    print(f"{'SimRV Simulation Speed':<30} | {mips32:>10.2f} MIPS | {mips64:>10.2f} MIPS | {'--':>20}")

    if args.spike and "spike" in res64 and "spike" in res32:
        sp_t64 = res64["spike"]["stats"]["time"]["mean"]
        sp_t32 = res32["spike"]["stats"]["time"]["mean"]
        sp_time_ratio = sp_t32 / sp_t64 if sp_t64 > 0 else 0.0
        spdup64 = sp_t64 / t64 if t64 > 0 and sp_t64 > 0 else 0.0
        spdup32 = sp_t32 / t32 if t32 > 0 and sp_t32 > 0 else 0.0
        print("-" * 90)
        print(f"{'Spike Wall Time (s)':<30} | {sp_t32:>15.4f} | {sp_t64:>15.4f} | {sp_time_ratio:>19.2f}x")
        print(f"{'SimRV Speedup vs Spike':<30} | {spdup32:>14.2f}x | {spdup64:>14.2f}x | {'--':>20}")

    print("=" * 90)
    print("Note: Instruction Ratio > 1.0x indicates multi-word arithmetic emulation cost on RV32.")
    print("=" * 90 + "\n")

    if args.json:
        report = {
            "benchmark": bench_name,
            "rv64": res64,
            "rv32": res32,
            "comparison": {
                "instruction_ratio": inst_ratio,
                "time_ratio": time_ratio,
            }
        }
        with open(args.json, "w") as f:
            json.dump(report, f, indent=2)
        print(f"Report written to {args.json}")


if __name__ == "__main__":
    main()
