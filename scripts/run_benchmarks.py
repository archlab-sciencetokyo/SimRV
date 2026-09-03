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

def run_suite(benchmarks, simrv_bin, spike_bin, runs=3, limit=50000000, timeout=60, mode=None, extra_args=None):
    script_dir = os.path.dirname(os.path.abspath(__file__))
    benchmark_py = os.path.join(script_dir, "benchmark.py")
    results = []

    for name, path, isa in benchmarks:
        if not os.path.isfile(path):
            print(f"Skipping {name}: file {path} not found.")
            continue

        mode_str = f" [mode: {mode}]" if mode else ""
        print(f"\n==========================================")
        print(f" Running Benchmark: {name}{mode_str}")
        print(f"==========================================")

        os.makedirs(os.path.join(script_dir, "../.bench_tmp"), exist_ok=True)
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
        if mode:
            cmd.extend(["--simrv-arg", f"--mode {mode}"])
        if extra_args:
            for ea in extra_args:
                cmd.extend(["--simrv-arg", ea])

        try:
            res = subprocess.run(cmd, check=True)
            if os.path.isfile(tmp_json):
                with open(tmp_json, "r") as f:
                    data = json.load(f)
                    data["display_name"] = name
                    if mode:
                        data["mode"] = mode
                    results.append(data)
        except Exception as e:
            print(f"Error running benchmark {name}: {e}")

    return results

def print_summary_table(results_64, results_32, baseline_report=None, regression_thresh=0.03):
    print("\n" + "="*95)
    print(f"{'SIMRV VS SPIKE EXTENDED BENCHMARK SUITE SUMMARY':^95}")
    print("="*95)
    print(f"{'Benchmark':<20} | {'Arch':<5} | {'Mode':<10} | {'SimRV (s)':<10} | {'Spike (s)':<10} | {'Speedup':<8} | {'Status':<8}")
    print("-" * 95)

    def print_rows(results, arch_str):
        for r in results:
            name = r.get("display_name", r.get("test_name", "Unknown"))
            mode = r.get("mode", "default")
            simrv_t = r["simrv"]["stats"]["time"]["mean"]
            spike_t = r["spike"]["stats"]["time"]["mean"]
            speedup = spike_t / simrv_t if simrv_t > 0 and spike_t > 0 else 0.0
            speedup_str = f"{speedup:.2f}x" if speedup > 0 else "N/A"

            status = "PASS"
            if baseline_report:
                b_results = baseline_report.get(f"{arch_str.lower()}_results", [])
                for br in b_results:
                    if br.get("display_name") == name and br.get("mode", "default") == mode:
                        base_t = br["simrv"]["stats"]["time"]["mean"]
                        if simrv_t > base_t * (1.0 + regression_thresh):
                            pct = ((simrv_t - base_t) / base_t) * 100
                            status = f"REGRESS(+{pct:.1f}%)"
                        break

            print(f"{name:<20} | {arch_str:<5} | {mode:<10} | {simrv_t:>10.4f} | {spike_t:>10.4f} | {speedup_str:>8} | {status:<8}")

    print_rows(results_64, "RV64")
    if results_32:
        print("-" * 95)
        print_rows(results_32, "RV32")

    print("="*95)
    print("Note: Speedup > 1.0x indicates SimRV wall-clock time is faster than Spike.")
    print("="*95 + "\n")

def main():
    parser = argparse.ArgumentParser(description="Extended Benchmark Suite Runner against Spike")
    parser.add_argument("--simrv64", default="./build/rv64-release/SimRV", help="Path to RV64 SimRV binary")
    parser.add_argument("--simrv32", default="./build/rv32-release/SimRV", help="Path to RV32 SimRV binary")
    parser.add_argument("--spike", default="spike", help="Path to Spike binary")
    parser.add_argument("-n", "--runs", type=int, default=3, help="Number of benchmark iterations")
    parser.add_argument("-e", "--limit", type=int, default=50000000, help="Instruction limit")
    parser.add_argument("--modes", nargs="+", default=[None], help="Modes to sweep across (e.g. fast detailed 3stage 5stage)")
    parser.add_argument("--baseline", help="Baseline JSON report to check for regressions")
    parser.add_argument("--regression-threshold", type=float, default=0.03, help="Regression alert threshold (default: 0.03 = 3%)")
    parser.add_argument("--json", help="Path to save consolidated JSON report")

    args = parser.parse_args()

    baseline_data = None
    if args.baseline and os.path.isfile(args.baseline):
        with open(args.baseline, "r") as f:
            baseline_data = json.load(f)

    results_64 = []
    results_32 = []

    for mode in args.modes:
        if os.path.isfile(args.simrv64):
            print(f"\n>>> Running RV64 Benchmark Suite (mode: {mode or 'default'}) <<<")
            results_64.extend(run_suite(DEFAULT_BENCHMARKS_RV64, args.simrv64, args.spike, runs=args.runs, limit=args.limit, mode=mode))

        if os.path.isfile(args.simrv32):
            print(f"\n>>> Running RV32 Benchmark Suite (mode: {mode or 'default'}) <<<")
            results_32.extend(run_suite(DEFAULT_BENCHMARKS_RV32, args.simrv32, args.spike, runs=args.runs, limit=args.limit, mode=mode))

    print_summary_table(results_64, results_32, baseline_report=baseline_data, regression_thresh=args.regression_threshold)

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
