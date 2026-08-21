#!/usr/bin/env python3
import sys
import os
import subprocess
import json
import argparse

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
TESTS_DIR = os.environ.get("TESTS_DIR", os.path.abspath(os.path.join(SCRIPT_DIR, "../../../tests")))


def resolve_bench_path(rel_path):
    candidates = [
        os.path.join(ROOT_DIR, rel_path),
        os.path.join(TESTS_DIR, rel_path),
        os.path.join(TESTS_DIR, "riscv-tests", rel_path),
    ]
    for cand in candidates:
        if os.path.isfile(cand):
            return cand
    return os.path.join(ROOT_DIR, rel_path)


DEFAULT_BENCHMARKS_RV64 = [
    ("AHA Montgomery (RV64)", resolve_bench_path("riscv-tests/benchmarks/aha-mont64.riscv"), "rv64gc"),
    ("CoreMark (RV64)", resolve_bench_path("coremark/coremark64.riscv"), "rv64gc"),
    ("Dhrystone", resolve_bench_path("riscv-tests/benchmarks/dhrystone.riscv"), "rv64gc"),
    ("Median Filter", resolve_bench_path("riscv-tests/benchmarks/median.riscv"), "rv64gc"),
    ("Memcpy", resolve_bench_path("riscv-tests/benchmarks/memcpy.riscv"), "rv64gc"),
    ("Matrix Multiply", resolve_bench_path("riscv-tests/benchmarks/mm.riscv"), "rv64gc"),
    ("Multiply", resolve_bench_path("riscv-tests/benchmarks/multiply.riscv"), "rv64gc"),
    ("Quicksort", resolve_bench_path("riscv-tests/benchmarks/qsort.riscv"), "rv64gc"),
    ("Radix Sort", resolve_bench_path("riscv-tests/benchmarks/rsort.riscv"), "rv64gc"),
    ("SPMV", resolve_bench_path("riscv-tests/benchmarks/spmv.riscv"), "rv64gc"),
    ("Towers of Hanoi", resolve_bench_path("riscv-tests/benchmarks/towers.riscv"), "rv64gc"),
    ("Vector Add", resolve_bench_path("riscv-tests/benchmarks/vvadd.riscv"), "rv64gc"),
]

DEFAULT_BENCHMARKS_RV32 = [
    ("AHA Montgomery (RV32)", resolve_bench_path("riscv-tests/benchmarks/aha-mont32.riscv"), "rv32imac"),
    ("CoreMark (RV32)", resolve_bench_path("coremark/coremark32.riscv"), "rv32imac"),
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
    print(f"{'Benchmark':<24} | {'Arch':<5} | {'Instructions':<12} | {'SimRV (s)':<10} | {'Spike (s)':<10} | {'Speedup':<8}")
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

            print(f"{name:<24} | {arch_str:<5} | {inst_str:>12} | {simrv_t:>10.4f} | {spike_t:>10.4f} | {speedup_str:>8}")

    print_rows(results_64, "RV64")
    if results_32:
        print("-" * 85)
        print_rows(results_32, "RV32")

    print("="*85)
    print("Note: Speedup > 1.0x indicates SimRV wall-clock time is faster than Spike.")
    print("="*85 + "\n")


def print_arch_comparison_table(results_64, results_32):
    # Match benchmarks by normalizing their names
    def normalize_name(n):
        return n.lower().replace("(rv64)", "").replace("(rv32)", "").replace("rv64", "").replace("rv32", "").strip()

    map_64 = {normalize_name(r.get("display_name", r.get("test_name", ""))): r for r in results_64}
    map_32 = {normalize_name(r.get("display_name", r.get("test_name", ""))): r for r in results_32}

    common_keys = [k for k in map_64 if k in map_32]
    if not common_keys:
        return

    print("="*92)
    print(f"{'32-BIT VS 64-BIT ARCHITECTURAL BENCHMARK COMPARISON':^92}")
    print("="*92)
    print(f"{'Benchmark':<20} | {'RV32 Insts':>11} | {'RV64 Insts':>11} | {'Inst Ratio':>10} | {'RV32 Time (s)':>13} | {'RV64 Time (s)':>13}")
    print("-" * 92)

    for k in common_keys:
        r32 = map_32[k]
        r64 = map_64[k]
        name = k.title()
        i32 = r32.get("instructions", 0)
        i64 = r64.get("instructions", 0)
        ratio_str = f"{i32 / i64:.2f}x" if i64 > 0 else "N/A"

        t32 = r32["simrv"]["stats"]["time"]["mean"]
        t64 = r64["simrv"]["stats"]["time"]["mean"]

        print(f"{name:<20} | {i32:>11,} | {i64:>11,} | {ratio_str:>10} | {t32:>13.4f} | {t64:>13.4f}")

    print("="*92)
    print("Note: Inst Ratio > 1.0x shows 32-bit instruction overhead vs native 64-bit operations.")
    print("="*92 + "\n")


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
    if results_64 and results_32:
        print_arch_comparison_table(results_64, results_32)

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
