#!/usr/bin/env python3
import sys
import os
import subprocess
import json
import argparse

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
TESTS_DIR = os.environ.get("TESTS_DIR", os.path.abspath(os.path.join(SCRIPT_DIR, "../../../tests")))

DEFAULT_BENCHMARKS_RV64 = [
    ("CoreMark (RV64)", os.path.join(TESTS_DIR, "coremark/coremark64.riscv"), "rv64gc"),
    ("Dhrystone", os.path.join(TESTS_DIR, "riscv-tests/benchmarks/dhrystone.riscv"), "rv64gc"),
    ("Median Filter", os.path.join(TESTS_DIR, "riscv-tests/benchmarks/median.riscv"), "rv64gc"),
    ("Memcpy", os.path.join(TESTS_DIR, "riscv-tests/benchmarks/memcpy.riscv"), "rv64gc"),
    ("Matrix Multiply", os.path.join(TESTS_DIR, "riscv-tests/benchmarks/mm.riscv"), "rv64gc"),
    ("Multiply", os.path.join(TESTS_DIR, "riscv-tests/benchmarks/multiply.riscv"), "rv64gc"),
    ("Quicksort", os.path.join(TESTS_DIR, "riscv-tests/benchmarks/qsort.riscv"), "rv64gc"),
    ("Radix Sort", os.path.join(TESTS_DIR, "riscv-tests/benchmarks/rsort.riscv"), "rv64gc"),
    ("SPMV", os.path.join(TESTS_DIR, "riscv-tests/benchmarks/spmv.riscv"), "rv64gc"),
    ("Towers of Hanoi", os.path.join(TESTS_DIR, "riscv-tests/benchmarks/towers.riscv"), "rv64gc"),
    ("Vector Add", os.path.join(TESTS_DIR, "riscv-tests/benchmarks/vvadd.riscv"), "rv64gc"),
]

DEFAULT_BENCHMARKS_RV32 = [
    ("CoreMark (RV32)", os.path.join(TESTS_DIR, "coremark/coremark32.riscv"), "rv32imac"),
]

def run_suite(benchmarks, simrv_bin, spike_bin, runs=3, limit=50000000, timeout=60):
    script_dir = os.path.dirname(os.path.abspath(__file__))
    benchmark_py = os.path.join(script_dir, "benchmark.py")
    results = []

    for name, path, isa in benchmarks:
        if not os.path.isfile(path):
            print(f"Skipping {name}: file {path} not found.")
            continue

        print(f"\n==========================================")
        print(f" Running Benchmark: {name}")
        print(f"==========================================")

        tmp_json = os.path.join(script_dir, "../.bench_tmp/last_run.json")
        cmd = [
            sys.executable, benchmark_py,
            "--simrv", simrv_bin,
            "--spike", spike_bin,
            "--test", path,
            "--runs", str(runs),
            "--limit", str(limit),
            "--timeout", str(timeout),
            "--isa", isa,
            "--json", tmp_json
        ]

        try:
            res = subprocess.run(cmd, check=True)
            if os.path.isfile(tmp_json):
                with open(tmp_json, "r") as f:
                    data = json.load(f)
                    data["display_name"] = name
                    results.append(data)
        except Exception as e:
            print(f"Error running benchmark {name}: {e}")

    return results

def print_summary_table(results_64, results_32):
    print("\n" + "="*85)
    print(f"{'SIMRV VS SPIKE EXTENDED BENCHMARK SUITE SUMMARY':^85}")
    print("="*85)
    print(f"{'Benchmark':<20} | {'Arch':<5} | {'Instructions':<12} | {'SimRV (s)':<10} | {'Spike (s)':<10} | {'Speedup':<8}")
    print("-" * 85)

    def print_rows(results, arch_str):
        for r in results:
            name = r.get("display_name", r.get("test_name", "Unknown"))
            insts = r.get("instructions", 0)
            simrv_t = r["simrv"]["stats"]["time"]["mean"]
            spike_t = r["spike"]["stats"]["time"]["mean"]
            speedup = spike_t / simrv_t if simrv_t > 0 and spike_t > 0 else 0.0
            
            inst_str = f"{insts:,}" if insts else "N/A"
            speedup_str = f"{speedup:.2f}x" if speedup > 0 else "N/A"

            print(f"{name:<20} | {arch_str:<5} | {inst_str:>12} | {simrv_t:>10.4f} | {spike_t:>10.4f} | {speedup_str:>8}")

    print_rows(results_64, "RV64")
    if results_32:
        print("-" * 85)
        print_rows(results_32, "RV32")

    print("="*85)
    print("Note: Speedup > 1.0x indicates SimRV wall-clock time is faster than Spike.")
    print("="*85 + "\n")

def main():
    parser = argparse.ArgumentParser(description="Extended Benchmark Suite Runner against Spike")
    parser.add_argument("--simrv64", default="./build/rv64-release/SimRV", help="Path to RV64 SimRV binary")
    parser.add_argument("--simrv32", default="./build/rv32-release/SimRV", help="Path to RV32 SimRV binary")
    parser.add_argument("--spike", default="spike", help="Path to Spike binary")
    parser.add_argument("-n", "--runs", type=int, default=3, help="Number of benchmark iterations")
    parser.add_argument("-e", "--limit", type=int, default=50000000, help="Instruction limit")
    parser.add_argument("--json", help="Path to save consolidated JSON report")

    args = parser.parse_args()

    results_64 = []
    results_32 = []

    if os.path.isfile(args.simrv64):
        print("\n>>> Running RV64 Benchmark Suite <<<")
        results_64 = run_suite(DEFAULT_BENCHMARKS_RV64, args.simrv64, args.spike, runs=args.runs, limit=args.limit)

    if os.path.isfile(args.simrv32):
        print("\n>>> Running RV32 Benchmark Suite <<<")
        results_32 = run_suite(DEFAULT_BENCHMARKS_RV32, args.simrv32, args.spike, runs=args.runs, limit=args.limit)

    print_summary_table(results_64, results_32)

    if args.json:
        report = {
            "rv64_results": results_64,
            "rv32_results": results_32
        }
        with open(args.json, "w") as f:
            json.dump(report, f, indent=2)
        print(f"Consolidated JSON report written to {args.json}")

if __name__ == "__main__":
    main()
