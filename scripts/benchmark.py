#!/usr/bin/env python3
"""
@file benchmark.py
@brief Comprehensive SimRV & Spike benchmarking suite with publication-ready paper outputs.
"""

import argparse
import csv
import json
import math
import os
import platform
import re
import statistics
import subprocess
import sys
import time
from shutil import which

REALWORLD_BENCHMARKS = [
    "dhrystone",
    "median",
    "memcpy",
    "mm",
    "multiply",
    "qsort",
    "rsort",
    "spmv",
    "towers",
    "vvadd",
]

ISA_SMOKE_BENCHMARKS = [
    "rv64ui-p-add",
    "rv64ui-p-sub",
    "rv64ui-p-mul",
    "rv64um-p-mul",
    "rv64ua-p-amoadd_w",
]


def is_executable(path):
    return path and os.path.isfile(path) and os.access(path, os.X_OK)


def get_riscv_prefix():
    prefix = os.environ.get("RISCV_PREFIX")
    if prefix:
        return prefix
    if which("riscv64-unknown-elf-gcc"):
        return "riscv64-unknown-elf-"
    return ""


def get_tool_path(tool_name, env_var, prefix):
    path = os.environ.get(env_var)
    if path:
        return path
    if prefix:
        full_name = f"{prefix}{tool_name}"
        if os.path.isabs(full_name) and is_executable(full_name):
            return full_name
        elif which(full_name):
            return which(full_name)

    for name in [
        f"riscv64-unknown-elf-{tool_name}",
        f"riscv32-unknown-elf-{tool_name}",
        tool_name,
    ]:
        w = which(name)
        if w:
            return w
    return None


def resolve_tohost(elf_path, nm_tool):
    if not nm_tool or not os.path.exists(elf_path):
        return None
    try:
        res = subprocess.run(
            [nm_tool, "-g", elf_path], capture_output=True, text=True, timeout=5
        )
        for line in res.stdout.splitlines():
            parts = line.split()
            if len(parts) >= 3 and parts[2] == "tohost":
                addr = parts[0].lstrip("0")
                return f"0x{addr}" if addr else "0x0"
    except Exception:
        pass
    return None


def detect_elf_xlen(elf_path):
    if elf_path and os.path.isfile(elf_path):
        try:
            with open(elf_path, "rb") as f:
                header = f.read(5)
                if len(header) >= 5 and header[:4] == b"\x7fELF":
                    return 64 if header[4] == 2 else 32
        except Exception:
            pass
    return None


def detect_xlen(simrv_bin, elf_path=None):
    elf_xlen = detect_elf_xlen(elf_path)
    if elf_xlen:
        return elf_xlen

    try:
        result = subprocess.run(
            [simrv_bin, "--version"], capture_output=True, text=True, timeout=5
        )
        if "RV64" in result.stdout:
            return 64
        elif "RV32" in result.stdout:
            return 32
    except Exception:
        pass
    if "rv64" in simrv_bin.lower():
        return 64
    return 32


def parse_simrv_output(output):
    instrs = None
    kips = None
    sim_time = None

    for line in output.splitlines():
        if "Executed instructions" in line:
            parts = line.split(":")
            if len(parts) > 1:
                match = re.search(r"\(([\d,]+)\)", parts[1])
                if match:
                    instrs = int(match.group(1).replace(",", ""))
                else:
                    match = re.search(r"(\d+)", parts[1])
                    if match:
                        instrs = int(match.group(1))

        if "Simulation speed" in line:
            parts = line.split(":")
            if len(parts) > 1:
                match = re.search(
                    r"([\d.,]+)\s+(MIPS|KIPS)", parts[1], re.IGNORECASE
                )
                if match:
                    val = float(match.group(1).replace(",", ""))
                    unit = match.group(2).upper()
                    kips = val * 1000.0 if unit == "MIPS" else val

        if "Elapsed time (real)" in line:
            parts = line.split(":")
            if len(parts) > 1:
                match = re.search(r"([\d.]+)\s+sec", parts[1])
                if match:
                    sim_time = float(match.group(1))
    return instrs, kips, sim_time


def calculate_stats(data_list):
    if not data_list:
        return {"mean": 0.0, "median": 0.0, "min": 0.0, "max": 0.0, "stddev": 0.0}

    mean = statistics.mean(data_list)
    median = statistics.median(data_list)
    minimum = min(data_list)
    maximum = max(data_list)
    stddev = statistics.stdev(data_list) if len(data_list) > 1 else 0.0

    return {
        "mean": mean,
        "median": median,
        "min": minimum,
        "max": maximum,
        "stddev": stddev,
    }


def calculate_geomean(values):
    valid = [v for v in values if v > 0]
    if not valid:
        return 0.0
    return math.exp(sum(math.log(v) for v in valid) / len(valid))


def format_instrs(insts):
    return f"{insts:,}" if insts is not None else "N/A"


def print_single_stats_table(simrv_stats, spike_stats, test_name, runs, insts):
    use_color = sys.stdout.isatty()
    BOLD = "\033[1m" if use_color else ""
    RESET = "\033[0m" if use_color else ""
    GREEN = "\033[32m" if use_color else ""
    CYAN = "\033[36m" if use_color else ""
    YELLOW = "\033[33m" if use_color else ""

    col_w1 = 28
    col_w2 = 18
    col_w3 = 18
    col_w4 = 18
    total_w = col_w1 + col_w2 + col_w3 + col_w4 + 9

    print(f"\n{BOLD}{'=' * total_w}{RESET}")
    print(f"{BOLD}{'SIMRV BENCHMARK REPORT':^{total_w}}{RESET}")
    print(f"{BOLD}{'=' * total_w}{RESET}")
    print(f"  Test Name    : {CYAN}{test_name}{RESET}")
    print(f"  Runs         : {runs}")
    print(f"  Instructions : {format_instrs(insts)}")
    print(f"{'-' * total_w}")

    header = f"{'Metric / Statistic':<{col_w1}} | {'SimRV':^{col_w2}} | {'Spike':^{col_w3}} | {'Speedup':^{col_w4}}"
    print(f"{BOLD}{header}{RESET}")
    print(f"{'-' * col_w1}-+-{'-' * col_w2}-+-{'-' * col_w3}-+-{'-' * col_w4}")

    def row(label, s_val, sp_val, spdup_val=""):
        return f"{label:<{col_w1}} | {s_val:^{col_w2}} | {sp_val:^{col_w3}} | {spdup_val:^{col_w4}}"

    # Real Time
    s_mean = simrv_stats["time"]["mean"]
    sp_mean = spike_stats["time"]["mean"]
    speedup_str = (
        f"{sp_mean / s_mean:.2f}x" if s_mean > 0 and sp_mean > 0 else "N/A"
    )

    print(row("Real Time (seconds)", "", "", ""))
    print(
        row(
            "  Mean",
            f"{simrv_stats['time']['mean']:.4f} s",
            f"{spike_stats['time']['mean']:.4f} s" if sp_mean > 0 else "N/A",
            speedup_str,
        )
    )
    print(
        row(
            "  Median",
            f"{simrv_stats['time']['median']:.4f} s",
            (
                f"{spike_stats['time']['median']:.4f} s"
                if spike_stats["time"]["median"] > 0
                else "N/A"
            ),
            "",
        )
    )
    print(
        row(
            "  Min / Max",
            f"{simrv_stats['time']['min']:.4f}/{simrv_stats['time']['max']:.4f}",
            (
                f"{spike_stats['time']['min']:.4f}/{spike_stats['time']['max']:.4f}"
                if sp_mean > 0
                else "N/A"
            ),
            "",
        )
    )
    print(
        row(
            "  Std Dev",
            f"{simrv_stats['time']['stddev']:.4f} s",
            (
                f"{spike_stats['time']['stddev']:.4f} s"
                if sp_mean > 0
                else "N/A"
            ),
            "",
        )
    )
    print(f"{'-' * col_w1}-+-{'-' * col_w2}-+-{'-' * col_w3}-+-{'-' * col_w4}")

    # Wall Speed
    s_wall_kips = simrv_stats["wall_speed"]["mean"]
    sp_wall_kips = spike_stats["wall_speed"]["mean"]
    speedup_wall = (
        f"{s_wall_kips / sp_wall_kips:.2f}x"
        if s_wall_kips > 0 and sp_wall_kips > 0
        else "N/A"
    )

    print(row("Wall Speed (KIPS)", "", "", ""))
    print(
        row(
            "  Mean",
            f"{s_wall_kips:,.1f} KIPS",
            f"{sp_wall_kips:,.1f} KIPS" if sp_wall_kips > 0 else "N/A",
            speedup_wall,
        )
    )
    print(
        row(
            "  Median",
            f"{simrv_stats['wall_speed']['median']:,.1f} KIPS",
            (
                f"{spike_stats['wall_speed']['median']:,.1f} KIPS"
                if sp_wall_kips > 0
                else "N/A"
            ),
            "",
        )
    )
    print(
        row(
            "  Min / Max",
            f"{simrv_stats['wall_speed']['min']:,.0f}/{simrv_stats['wall_speed']['max']:,.0f}",
            (
                f"{spike_stats['wall_speed']['min']:,.0f}/{spike_stats['wall_speed']['max']:,.0f}"
                if sp_wall_kips > 0
                else "N/A"
            ),
            "",
        )
    )
    print(
        row(
            "  Std Dev",
            f"{simrv_stats['wall_speed']['stddev']:,.1f} KIPS",
            (
                f"{spike_stats['wall_speed']['stddev']:,.1f} KIPS"
                if sp_wall_kips > 0
                else "N/A"
            ),
            "",
        )
    )

    # Core Sim Speed
    if "sim_speed" in simrv_stats and simrv_stats["sim_speed"]["mean"] > 0:
        print(f"{'-' * col_w1}-+-{'-' * col_w2}-+-{'-' * col_w3}-+-{'-' * col_w4}")
        print(row("Sim Core Speed (KIPS)", "", "", ""))
        print(
            row(
                "  Mean Core Speed",
                f"{simrv_stats['sim_speed']['mean']:,.1f} KIPS",
                "N/A",
                "",
            )
        )

    print(f"{BOLD}{'=' * total_w}{RESET}")
    if s_mean > 0 and sp_mean > 0:
        speedup = sp_mean / s_mean
        if speedup >= 1.0:
            print(
                f"  {GREEN}[SUMMARY] SimRV is {speedup:.2f}x FASTER than Spike (wall-clock time){RESET}"
            )
        else:
            print(
                f"  {YELLOW}[SUMMARY] Spike is {1.0 / speedup:.2f}x FASTER than SimRV (wall-clock time){RESET}"
            )
    print(f"{BOLD}{'=' * total_w}{RESET}\n")


def print_suite_stats_table(suite_results):
    use_color = sys.stdout.isatty()
    BOLD = "\033[1m" if use_color else ""
    RESET = "\033[0m" if use_color else ""
    GREEN = "\033[32m" if use_color else ""

    col_w1 = 20
    col_w2 = 14
    col_w3 = 14
    col_w4 = 14
    col_w5 = 16
    col_w6 = 12
    total_w = col_w1 + col_w2 + col_w3 + col_w4 + col_w5 + col_w6 + 15

    print(f"\n{BOLD}{'=' * total_w}{RESET}")
    print(f"{BOLD}{'SIMRV BENCHMARK SUITE PAPER SUMMARY':^{total_w}}{RESET}")
    print(f"{BOLD}{'=' * total_w}{RESET}")

    header = f"{'Benchmark':<{col_w1}} | {'Instructions':^{col_w2}} | {'SimRV (s)':^{col_w3}} | {'Spike (s)':^{col_w4}} | {'SimRV (MIPS)':^{col_w5}} | {'Speedup':^{col_w6}}"
    print(f"{BOLD}{header}{RESET}")
    print(
        f"{'-' * col_w1}-+-{'-' * col_w2}-+-{'-' * col_w3}-+-{'-' * col_w4}-+-{'-' * col_w5}-+-{'-' * col_w6}"
    )

    speedups = []
    for res in suite_results:
        name = res["test_name"]
        insts = format_instrs(res["instructions"])
        s_time = res["simrv"]["stats"]["time"]["mean"]
        sp_time = res["spike"]["stats"]["time"]["mean"]
        s_mips = res["simrv"]["stats"]["wall_speed"]["mean"] / 1000.0

        if s_time > 0 and sp_time > 0:
            spdup_val = sp_time / s_time
            speedups.append(spdup_val)
            spdup_str = f"{spdup_val:.2f}x"
        else:
            spdup_str = "N/A"

        sp_time_str = f"{sp_time:.4f}" if sp_time > 0 else "N/A"
        print(
            f"{name:<{col_w1}} | {insts:>{col_w2}} | {s_time:>{col_w3}.4f} | {sp_time_str:>{col_w4}} | {s_mips:>{col_w5}.2f} | {spdup_str:>{col_w6}}"
        )

    print(
        f"{'-' * col_w1}-+-{'-' * col_w2}-+-{'-' * col_w3}-+-{'-' * col_w4}-+-{'-' * col_w5}-+-{'-' * col_w6}"
    )
    geomean_speedup = calculate_geomean(speedups)
    geomean_str = f"{geomean_speedup:.2f}x" if geomean_speedup > 0 else "N/A"
    print(
        f"{BOLD}{'Geometric Mean':<{col_w1}} | {'--':>{col_w2}} | {'--':>{col_w3}} | {'--':>{col_w4}} | {'--':>{col_w5}} | {geomean_str:>{col_w6}}{RESET}"
    )
    print(f"{BOLD}{'=' * total_w}{RESET}")
    if geomean_speedup > 0:
        print(
            f"  {GREEN}[SUITE GEOMEAN] SimRV achieves an overall {geomean_speedup:.2f}x speedup over Spike.{RESET}"
        )
    print(f"{BOLD}{'=' * total_w}{RESET}\n")


def generate_latex_table(suite_results, filepath):
    speedups = []
    lines = [
        r"\begin{table}[htbp]",
        r"\centering",
        r"\caption{Performance Evaluation of SimRV vs. Spike across RISC-V Benchmarks}",
        r"\label{tab:simrv_benchmark_results}",
        r"\begin{tabular}{lrrccr}",
        r"\toprule",
        r"\textbf{Benchmark} & \textbf{Instructions} & \textbf{SimRV (s)} & \textbf{Spike (s)} & \textbf{SimRV (MIPS)} & \textbf{Speedup} \\",
        r"\midrule",
    ]

    for res in suite_results:
        name = res["test_name"].replace("_", r"\_")
        insts = format_instrs(res["instructions"])
        s_time = res["simrv"]["stats"]["time"]["mean"]
        sp_time = res["spike"]["stats"]["time"]["mean"]
        s_mips = res["simrv"]["stats"]["wall_speed"]["mean"] / 1000.0

        if s_time > 0 and sp_time > 0:
            spdup_val = sp_time / s_time
            speedups.append(spdup_val)
            spdup_str = f"{spdup_val:.2f}\\times"
        else:
            spdup_str = r"\text{N/A}"

        sp_time_str = f"{sp_time:.4f}" if sp_time > 0 else r"\text{N/A}"
        lines.append(
            f"{name} & {insts} & {s_time:.4f} & {sp_time_str} & {s_mips:.2f} & {spdup_str} \\\\"
        )

    geomean_speedup = calculate_geomean(speedups)
    geomean_str = (
        f"\\textbf{{{geomean_speedup:.2f}\\times}}"
        if geomean_speedup > 0
        else r"\text{N/A}"
    )

    lines.extend(
        [
            r"\midrule",
            f"\\textbf{{Geometric Mean}} & -- & -- & -- & -- & {geomean_str} \\\\",
            r"\bottomrule",
            r"\end{tabular}",
            r"\end{table}",
        ]
    )

    os.makedirs(os.path.dirname(os.path.abspath(filepath)), exist_ok=True)
    with open(filepath, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"LaTeX table written to: {filepath}")


def generate_markdown_table(suite_results, filepath):
    speedups = []
    lines = [
        "| Benchmark | Instructions | SimRV Time (s) | Spike Time (s) | SimRV Speed (MIPS) | Speedup |",
        "| :--- | ---: | ---: | ---: | ---: | ---: |",
    ]

    for res in suite_results:
        name = res["test_name"]
        insts = format_instrs(res["instructions"])
        s_time = res["simrv"]["stats"]["time"]["mean"]
        sp_time = res["spike"]["stats"]["time"]["mean"]
        s_mips = res["simrv"]["stats"]["wall_speed"]["mean"] / 1000.0

        if s_time > 0 and sp_time > 0:
            spdup_val = sp_time / s_time
            speedups.append(spdup_val)
            spdup_str = f"{spdup_val:.2f}x"
        else:
            spdup_str = "N/A"

        sp_time_str = f"{sp_time:.4f}" if sp_time > 0 else "N/A"
        lines.append(
            f"| {name} | {insts} | {s_time:.4f} | {sp_time_str} | {s_mips:.2f} | {spdup_str} |"
        )

    geomean_speedup = calculate_geomean(speedups)
    geomean_str = f"**{geomean_speedup:.2f}x**" if geomean_speedup > 0 else "N/A"
    lines.append(
        f"| **Geometric Mean** | **--** | **--** | **--** | **--** | {geomean_str} |"
    )

    os.makedirs(os.path.dirname(os.path.abspath(filepath)), exist_ok=True)
    with open(filepath, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"Markdown table written to: {filepath}")


def generate_csv_report(suite_results, filepath):
    os.makedirs(os.path.dirname(os.path.abspath(filepath)), exist_ok=True)
    with open(filepath, "w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(
            [
                "Benchmark",
                "Instructions",
                "SimRV_Time_Mean_s",
                "SimRV_Time_StdDev_s",
                "Spike_Time_Mean_s",
                "Spike_Time_StdDev_s",
                "SimRV_Wall_KIPS",
                "SimRV_MIPS",
                "Spike_Wall_KIPS",
                "Speedup_Ratio",
            ]
        )

        for res in suite_results:
            name = res["test_name"]
            insts = res["instructions"] or 0
            s_time = res["simrv"]["stats"]["time"]["mean"]
            s_time_sd = res["simrv"]["stats"]["time"]["stddev"]
            sp_time = res["spike"]["stats"]["time"]["mean"]
            sp_time_sd = res["spike"]["stats"]["time"]["stddev"]
            s_kips = res["simrv"]["stats"]["wall_speed"]["mean"]
            sp_kips = res["spike"]["stats"]["wall_speed"]["mean"]
            spdup = sp_time / s_time if s_time > 0 and sp_time > 0 else 0.0

            writer.writerow(
                [
                    name,
                    insts,
                    f"{s_time:.6f}",
                    f"{s_time_sd:.6f}",
                    f"{sp_time:.6f}",
                    f"{sp_time_sd:.6f}",
                    f"{s_kips:.2f}",
                    f"{s_kips / 1000.0:.2f}",
                    f"{sp_kips:.2f}",
                    f"{spdup:.4f}",
                ]
            )
    print(f"CSV report written to: {filepath}")


def run_benchmark_single(
    test_target,
    simrv_bin,
    spike_bin,
    runs,
    limit,
    timeout,
    tohost_arg,
    riscv_tests_dir,
    root_dir,
    nm_tool,
    objcopy_tool,
    isa_override,
    warmups,
):
    # Resolve ELF path
    elf_path = test_target
    if not os.path.isfile(elf_path):
        candidates = [
            os.path.join(riscv_tests_dir, "benchmarks", test_target),
            os.path.join(
                riscv_tests_dir, "benchmarks", f"{test_target}.riscv"
            ),
            os.path.join(riscv_tests_dir, "isa", test_target),
            os.path.join(riscv_tests_dir, test_target),
            os.path.join(riscv_tests_dir, f"{test_target}.riscv"),
        ]
        for cand in candidates:
            if os.path.isfile(cand):
                elf_path = cand
                break

    if not os.path.isfile(elf_path):
        print(f"ERROR: Benchmark ELF file not found: {test_target}", file=sys.stderr)
        return None

    test_basename = os.path.basename(elf_path)
    work_dir = os.path.join(root_dir, ".bench_tmp")
    log_dir = os.path.join(root_dir, "benchmark_logs")
    os.makedirs(work_dir, exist_ok=True)
    os.makedirs(log_dir, exist_ok=True)

    bin_path = os.path.join(work_dir, f"{test_basename}.bin")
    try:
        subprocess.run(
            [objcopy_tool, "-O", "binary", elf_path, bin_path], check=True
        )
    except Exception as e:
        print(f"ERROR: objcopy conversion failed: {e}", file=sys.stderr)
        return None

    tohost_addr = (
        tohost_arg
        or resolve_tohost(elf_path, nm_tool)
        or "0x80001000"
    )
    elf_xlen = detect_elf_xlen(elf_path)
    if elf_xlen:
        alt_bin = os.path.join(root_dir, f"build/rv{elf_xlen}-release/SimRV")
        if is_executable(alt_bin):
            simrv_bin = alt_bin

    xlen = detect_xlen(simrv_bin, elf_path)
    isa = isa_override or f"rv{xlen}gc"

    simrv_times = []
    simrv_sim_times = []
    simrv_speeds = []
    simrv_instrs = None

    print(f"\n--- Running Benchmark Target: {test_basename} ---")
    print(f"  SimRV binary : {simrv_bin} (RV{xlen})")
    print(f"  ELF path     : {elf_path}")
    print(f"  Tohost addr  : {tohost_addr}")

    for _ in range(warmups):
        warmup = subprocess.run(
            [simrv_bin, "--cli", "-m", elf_path, "-e", str(limit), "-b", "-H", tohost_addr],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=timeout,
            stdin=subprocess.DEVNULL,
        )
        if warmup.returncode != 0:
            print(f"  [ERROR] SimRV warmup failed on {test_basename}", file=sys.stderr)
            return None

    # SimRV runs
    for i in range(1, runs + 1):
        log_file = os.path.join(log_dir, f"bench_simrv_{test_basename}_{i}.log")
        simrv_cmd = [
            simrv_bin,
            "--cli",
            "-m",
            elf_path,
            "-e",
            str(limit),
            "-b",
            "-H",
            tohost_addr,
        ]

        start_t = time.perf_counter()
        try:
            res = subprocess.run(
                simrv_cmd,
                capture_output=True,
                text=True,
                timeout=timeout,
                stdin=subprocess.DEVNULL,
            )
            end_t = time.perf_counter()

            with open(log_file, "w") as lf:
                lf.write(res.stdout)
                lf.write(res.stderr)

            if res.returncode != 0:
                print(
                    f"  [ERROR] SimRV failed on {test_basename} (code {res.returncode}). Log: {log_file}",
                    file=sys.stderr,
                )
                return None

            insts, kips, sim_time = parse_simrv_output(
                res.stdout + "\n" + res.stderr
            )
            if kips is None:
                print(
                    f"  [ERROR] Could not parse performance speed from SimRV output. Log: {log_file}",
                    file=sys.stderr,
                )
                return None

            elapsed = end_t - start_t
            simrv_times.append(elapsed)
            simrv_speeds.append(kips)
            simrv_sim_times.append(sim_time if sim_time is not None else 0.0)
            if insts:
                simrv_instrs = insts
        except subprocess.TimeoutExpired:
            print(
                f"  [ERROR] SimRV timed out on {test_basename} (iter {i})",
                file=sys.stderr,
            )
            return None

    # Spike runs
    spike_times = []
    spike_speeds = []
    spike_available = spike_bin and (is_executable(spike_bin) or which(spike_bin))

    if spike_available:
        for i in range(1, runs + 1):
            spike_cmd = [spike_bin, f"--isa={isa}"]
            spike_cmd.append(elf_path)
            start_t = time.perf_counter()
            try:
                res = subprocess.run(
                    spike_cmd,
                    capture_output=True,
                    timeout=timeout,
                    stdin=subprocess.DEVNULL,
                )
                end_t = time.perf_counter()
                if res.returncode != 0:
                    err_msg = res.stderr.decode("utf-8", errors="ignore").strip()
                    print(
                        f"  [ERROR] Spike failed on {test_basename} (code {res.returncode}): {err_msg}",
                        file=sys.stderr,
                    )
                    return None

                elapsed = end_t - start_t
                spike_times.append(elapsed)

                if simrv_instrs:
                    spike_kips = (simrv_instrs / elapsed) / 1000.0
                    spike_speeds.append(spike_kips)
                else:
                    spike_speeds.append(0.0)
            except subprocess.TimeoutExpired:
                print(
                    f"  [ERROR] Spike timed out on {test_basename} (iter {i})",
                    file=sys.stderr,
                )
                return None

    simrv_wall_speeds = (
        [(simrv_instrs / t) / 1000.0 for t in simrv_times]
        if simrv_instrs
        else [0.0] * len(simrv_times)
    )

    simrv_stats = {
        "time": calculate_stats(simrv_times),
        "sim_time": calculate_stats(simrv_sim_times),
        "sim_speed": calculate_stats(simrv_speeds),
        "wall_speed": calculate_stats(simrv_wall_speeds),
        "overhead": calculate_stats(
            [wall - sim for wall, sim in zip(simrv_times, simrv_sim_times)]
        ),
    }

    spike_stats = {
        "time": calculate_stats(spike_times),
        "wall_speed": calculate_stats(spike_speeds),
    }

    return {
        "test_name": test_basename,
        "runs": runs,
        "instructions": simrv_instrs,
        "xlen": xlen,
        "spike_isa": isa,
        "simrv": {
            "runs_time": simrv_times,
            "runs_sim_speed_kips": simrv_speeds,
            "runs_wall_speed_kips": simrv_wall_speeds,
            "stats": simrv_stats,
        },
        "spike": {
            "runs_time": spike_times,
            "runs_wall_speed_kips": spike_speeds,
            "stats": spike_stats,
        },
    }


def main():
    parser = argparse.ArgumentParser(
        description="SimRV & Spike Publication-Ready Benchmarking Suite"
    )
    parser.add_argument("--simrv", help="Path to SimRV executable")
    parser.add_argument(
        "--spike", help="Optional path to a compatible Spike executable"
    )
    parser.add_argument(
        "-n", "--runs", type=int, default=5, help="Number of benchmark iterations"
    )
    parser.add_argument(
        "--warmups", type=int, default=1, help="Unmeasured warmup runs per benchmark"
    )
    parser.add_argument(
        "-t",
        "--test",
        default="dhrystone",
        help="Single benchmark target or ELF path",
    )
    parser.add_argument(
        "--suite",
        choices=["realworld", "isa", "all"],
        help="Run an entire benchmark suite",
    )
    parser.add_argument(
        "--list-benchmarks",
        action="store_true",
        help="List available real-world benchmarks and exit",
    )
    parser.add_argument("-H", "--tohost", help="Custom tohost MMIO address")
    parser.add_argument(
        "-e",
        "--limit",
        type=int,
        default=20000000,
        help="Instruction limit cap per run",
    )
    parser.add_argument(
        "--timeout", type=int, default=30, help="Run timeout in seconds"
    )
    parser.add_argument(
        "--riscv-tests-dir", help="Path to riscv-tests directory"
    )
    parser.add_argument("--json", help="Path to export JSON benchmark report")
    parser.add_argument("--latex", help="Path to export LaTeX paper table (.tex)")
    parser.add_argument(
        "--markdown", help="Path to export Markdown paper table (.md)"
    )
    parser.add_argument("--csv", help="Path to export raw CSV benchmark report")
    parser.add_argument("--isa", help="Override Spike ISA string (e.g. rv64gc)")

    args = parser.parse_args()

    if args.list_benchmarks:
        print("Available Real-World Benchmarks:")
        for bm in REALWORLD_BENCHMARKS:
            print(f"  - {bm}")
        print("\nAvailable ISA Smoke Benchmarks:")
        for bm in ISA_SMOKE_BENCHMARKS:
            print(f"  - {bm}")
        sys.exit(0)

    if args.runs <= 0:
        print("ERROR: --runs must be a positive integer", file=sys.stderr)
        sys.exit(1)
    if args.warmups < 0:
        print("ERROR: --warmups cannot be negative", file=sys.stderr)
        sys.exit(1)

    script_dir = os.path.dirname(os.path.abspath(__file__))
    root_dir = os.path.dirname(script_dir)

    # 1. Resolve riscv-tests directory
    default_dir = os.path.join(root_dir, "../../tests/riscv-tests")
    if args.riscv_tests_dir:
        riscv_tests_dir = args.riscv_tests_dir
    elif os.path.isdir(default_dir):
        riscv_tests_dir = default_dir
    else:
        riscv_tests_dir = os.environ.get("RISCV_TESTS_DIR") or default_dir

    if os.path.isdir(os.path.join(riscv_tests_dir, "share", "riscv-tests")):
        riscv_tests_dir = os.path.join(riscv_tests_dir, "share", "riscv-tests")

    # 2. Resolve tools
    prefix = get_riscv_prefix()
    objcopy_tool = get_tool_path("objcopy", "RISCV_OBJCOPY", prefix)
    nm_tool = get_tool_path("nm", "RISCV_NM", prefix)

    if not objcopy_tool:
        print(
            "ERROR: objcopy tool not found. Set RISCV_OBJCOPY or RISCV_PREFIX.",
            file=sys.stderr,
        )
        sys.exit(2)

    # 3. Resolve SimRV binary
    simrv_bin = args.simrv
    if not simrv_bin:
        simrv_64 = os.path.join(root_dir, "build/rv64-release/SimRV")
        simrv_32 = os.path.join(root_dir, "build/rv32-release/SimRV")
        if is_executable(simrv_64):
            simrv_bin = simrv_64
        elif is_executable(simrv_32):
            simrv_bin = simrv_32
        else:
            simrv_bin = os.path.join(root_dir, "SimRV")

    if not is_executable(simrv_bin):
        print(
            f"ERROR: SimRV binary not executable: {simrv_bin}", file=sys.stderr
        )
        sys.exit(1)

    # Determine targets to benchmark
    targets = []
    if args.suite == "realworld":
        targets = REALWORLD_BENCHMARKS
    elif args.suite == "isa":
        targets = ISA_SMOKE_BENCHMARKS
    elif args.suite == "all":
        targets = REALWORLD_BENCHMARKS + ISA_SMOKE_BENCHMARKS
    else:
        targets = [args.test]

    suite_results = []

    for target in targets:
        result = run_benchmark_single(
            target,
            simrv_bin,
            args.spike,
            args.runs,
            args.limit,
            args.timeout,
            args.tohost,
            riscv_tests_dir,
            root_dir,
            nm_tool,
            objcopy_tool,
            args.isa,
            args.warmups,
        )
        if result:
            suite_results.append(result)
            if len(targets) == 1:
                print_single_stats_table(
                    result["simrv"]["stats"],
                    result["spike"]["stats"],
                    result["test_name"],
                    args.runs,
                    result["instructions"],
                )

    if len(suite_results) != len(targets):
        print(
            f"ERROR: benchmark suite incomplete ({len(suite_results)}/{len(targets)} targets succeeded)",
            file=sys.stderr,
        )
        sys.exit(1)

    if len(targets) > 1 and suite_results:
        print_suite_stats_table(suite_results)

    # Export formats
    if args.latex and suite_results:
        generate_latex_table(suite_results, args.latex)

    if args.markdown and suite_results:
        generate_markdown_table(suite_results, args.markdown)

    if args.csv and suite_results:
        generate_csv_report(suite_results, args.csv)

    if args.json and suite_results:
        os.makedirs(os.path.dirname(os.path.abspath(args.json)), exist_ok=True)
        report_data = (
            suite_results[0]
            if len(suite_results) == 1
            else {
                "environment": {
                    "platform": platform.platform(),
                    "processor": platform.processor(),
                    "python": platform.python_version(),
                    "runs": args.runs,
                    "warmups": args.warmups,
                    "instruction_limit": args.limit,
                },
                "suite_results": suite_results,
            }
        )
        with open(args.json, "w") as jf:
            json.dump(report_data, jf, indent=2)
        print(f"JSON report written to: {args.json}")


if __name__ == "__main__":
    main()
