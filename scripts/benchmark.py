#!/usr/bin/env python3
"""
@file benchmark.py
@brief Comprehensive SimRV & Spike benchmarking suite with publication-ready paper outputs.
"""

import argparse
import csv
import hashlib
import html
import json
import math
import os
import pathlib
import platform
import random
import re
import statistics
import subprocess
import sys
import time
from shutil import which

# Installed command-line tools live in <prefix>/bin while their supporting
# modules live in <prefix>/share/SimRV/scripts. Keep the in-tree path first so
# development and packaged execution use the same entry point.
_here = pathlib.Path(__file__).resolve().parent
for _candidate in (_here, _here.parent / "share" / "SimRV" / "scripts"):
    if (_candidate / "experiment_metadata.py").is_file():
        if str(_candidate) not in sys.path:
            sys.path.insert(0, str(_candidate))
        break

from experiment_metadata import experiment_provenance

REALWORLD_BENCHMARKS = [
    "coremark",
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


def is_executable(path):
    return path and os.path.isfile(path) and os.access(path, os.X_OK)


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def command_version(executable):
    if not executable:
        return "unknown"
    probes = ([executable, "--version"], [executable, "--help"])
    fallback = "unknown"
    for command in probes:
        try:
            result = subprocess.run(
                command, capture_output=True, text=True, timeout=5, check=False
            )
        except (OSError, subprocess.TimeoutExpired):
            continue
        output = (result.stdout or result.stderr).strip().splitlines()
        if not output:
            continue
        first_line = output[0].strip()
        if result.returncode == 0:
            return first_line
        if fallback == "unknown" and "unrecognized option" not in first_line.lower():
            fallback = first_line
    return fallback


def repository_revision(root_dir):
    try:
        return subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=root_dir, capture_output=True, text=True,
            timeout=5, check=True
        ).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return "unknown"


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
        f"riscv64-linux-gnu-{tool_name}",
        f"riscv32-linux-gnu-{tool_name}",
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
            [nm_tool, "-g", elf_path], capture_output=True, text=True, timeout=5, check=False
        )
        for line in res.stdout.splitlines():
            parts = line.split()
            if len(parts) >= 3 and parts[2] == "tohost":
                addr = parts[0].lstrip("0")
                return f"0x{addr}" if addr else "0x0"
    except (OSError, subprocess.SubprocessError):
        return None
    return None


def detect_elf_xlen(elf_path):
    if elf_path and os.path.isfile(elf_path):
        try:
            with open(elf_path, "rb") as f:
                header = f.read(5)
                if len(header) >= 5 and header[:4] == b"\x7fELF":
                    return 64 if header[4] == 2 else 32
        except OSError:
            return None
    return None


def detect_xlen(simrv_bin, elf_path=None):
    elf_xlen = detect_elf_xlen(elf_path)
    if elf_xlen:
        return elf_xlen

    try:
        result = subprocess.run(
            [simrv_bin, "--version"], capture_output=True, text=True, timeout=5, check=False
        )
        if "RV64" in result.stdout:
            return 64
        elif "RV32" in result.stdout:
            return 32
    except (OSError, subprocess.SubprocessError):
        return 64 if "rv64" in simrv_bin.lower() else 32
    if "rv64" in simrv_bin.lower():
        return 64
    return 32


def parse_simrv_output(output):
    instrs = None
    cycles = None
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

        if "Elapsed cycles (clocks)" in line:
            parts = line.split(":")
            if len(parts) > 1:
                match = re.search(r"\(([\d,]+)\)", parts[1])
                if match:
                    cycles = int(match.group(1).replace(",", ""))

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
    return instrs, cycles, kips, sim_time


def parse_perf_stat(path):
    """Parse perf stat's semicolon-delimited machine-readable output."""
    counters = {}
    with open(path, encoding="utf-8") as source:
        for raw_line in source:
            line = raw_line.strip()
            if not line or line.startswith("#"):
                continue
            fields = [field.strip() for field in line.split(";")]
            if len(fields) < 3 or fields[0].startswith("<"):
                continue
            try:
                value = float(fields[0])
            except ValueError:
                continue
            event = fields[2]
            sample = {
                "value": int(value) if value.is_integer() else value,
                "unit": fields[1] or None,
            }
            if len(fields) > 3 and fields[3]:
                try:
                    sample["time_enabled_ns"] = int(float(fields[3]))
                except ValueError:
                    pass
            if len(fields) > 4 and fields[4]:
                try:
                    sample["running_percent"] = float(fields[4].rstrip("%"))
                except ValueError:
                    pass
            counters[event] = sample
    return counters


def wrap_measured_command(command, perf_bin, perf_events, perf_output, use_time):
    if perf_bin:
        command = [
            perf_bin,
            "stat",
            "--no-big-num",
            "-x",
            ";",
            "-e",
            perf_events,
            "-o",
            perf_output,
            "--",
            *command,
        ]
    if use_time:
        command = ["/usr/bin/time", "-f", "__MAX_RSS_KB__:%M", *command]
    return command


def calculate_stats(data_list):
    if not data_list:
        return {
            "mean": 0.0,
            "median": 0.0,
            "min": 0.0,
            "max": 0.0,
            "stddev": 0.0,
            "ci95": 0.0,
            "cv": 0.0,
        }

    n = len(data_list)
    mean = statistics.mean(data_list)
    median = statistics.median(data_list)
    minimum = min(data_list)
    maximum = max(data_list)
    stddev = statistics.stdev(data_list) if n > 1 else 0.0
    ci95 = 1.96 * (stddev / math.sqrt(n)) if n > 1 else 0.0
    cv = (stddev / mean * 100.0) if mean > 0 else 0.0

    return {
        "mean": mean,
        "median": median,
        "min": minimum,
        "max": maximum,
        "stddev": stddev,
        "ci95": ci95,
        "cv": cv,
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
    col_w2 = 22
    col_w3 = 22
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
    s_ci = simrv_stats["time"]["ci95"]
    sp_mean = spike_stats["time"]["mean"]
    sp_ci = spike_stats["time"]["ci95"]
    speedup_str = (
        f"{sp_mean / s_mean:.2f}x" if s_mean > 0 and sp_mean > 0 else "N/A"
    )

    print(row("Real Time (seconds)", "", "", ""))
    print(
        row(
            "  Mean (±95% CI)",
            f"{s_mean:.4f} ± {s_ci:.4f} s",
            f"{sp_mean:.4f} ± {sp_ci:.4f} s" if sp_mean > 0 else "N/A",
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
            "  Std Dev (CV%)",
            f"{simrv_stats['time']['stddev']:.4f} s ({simrv_stats['time']['cv']:.1f}%)",
            (
                f"{spike_stats['time']['stddev']:.4f} s ({spike_stats['time']['cv']:.1f}%)"
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

    # Peak RSS (MB)
    if "rss" in simrv_stats and simrv_stats["rss"]["mean"] > 0:
        print(f"{'-' * col_w1}-+-{'-' * col_w2}-+-{'-' * col_w3}-+-{'-' * col_w4}")
        print(row("Memory Footprint", "", "", ""))
        s_rss = simrv_stats["rss"]["mean"]
        sp_rss = spike_stats.get("rss", {}).get("mean", 0.0)
        sp_rss_str = f"{sp_rss:.2f} MB" if sp_rss > 0 else "N/A"
        print(
            row(
                "  Peak RSS (MB)",
                f"{s_rss:.2f} MB",
                sp_rss_str,
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

    col_w1 = 18
    col_w2 = 12
    col_w3 = 18
    col_w4 = 18
    col_w5 = 14
    col_w6 = 12
    col_w7 = 12
    col_w8 = 10
    total_w = col_w1 + col_w2 + col_w3 + col_w4 + col_w5 + col_w6 + col_w7 + col_w8 + 21

    print(f"\n{BOLD}{'=' * total_w}{RESET}")
    print(f"{BOLD}{'SIMRV BENCHMARK SUITE PAPER SUMMARY':^{total_w}}{RESET}")
    print(f"{BOLD}{'=' * total_w}{RESET}")

    header = f"{'Benchmark':<{col_w1}} | {'Instructions':^{col_w2}} | {'SimRV (s)':^{col_w3}} | {'Spike (s)':^{col_w4}} | {'SimRV (MIPS)':^{col_w5}} | {'SimRV RSS':^{col_w6}} | {'Spike RSS':^{col_w7}} | {'Speedup':^{col_w8}}"
    print(f"{BOLD}{header}{RESET}")
    print(
        f"{'-' * col_w1}-+-{'-' * col_w2}-+-{'-' * col_w3}-+-{'-' * col_w4}-+-{'-' * col_w5}-+-{'-' * col_w6}-+-{'-' * col_w7}-+-{'-' * col_w8}"
    )

    speedups = []
    for res in suite_results:
        name = res["test_name"]
        insts = format_instrs(res["instructions"])
        s_time = res["simrv"]["stats"]["time"]["mean"]
        s_ci = res["simrv"]["stats"]["time"]["ci95"]
        sp_time = res["spike"]["stats"]["time"]["mean"]
        sp_ci = res["spike"]["stats"]["time"]["ci95"]
        s_mips = res["simrv"]["stats"]["wall_speed"]["mean"] / 1000.0
        s_rss = res["simrv"]["stats"].get("rss", {}).get("mean", 0.0)
        sp_rss = res["spike"]["stats"].get("rss", {}).get("mean", 0.0)

        if s_time > 0 and sp_time > 0:
            spdup_val = sp_time / s_time
            speedups.append(spdup_val)
            spdup_str = f"{spdup_val:.2f}x"
        else:
            spdup_str = "N/A"

        s_time_str = f"{s_time:.4f}±{s_ci:.4f}" if s_ci > 0 else f"{s_time:.4f}"
        sp_time_str = f"{sp_time:.4f}±{sp_ci:.4f}" if sp_time > 0 else "N/A"
        s_rss_str = f"{s_rss:.1f} MB" if s_rss > 0 else "--"
        sp_rss_str = f"{sp_rss:.1f} MB" if sp_rss > 0 else "--"

        print(
            f"{name:<{col_w1}} | {insts:>{col_w2}} | {s_time_str:>{col_w3}} | {sp_time_str:>{col_w4}} | {s_mips:>{col_w5}.2f} | {s_rss_str:>{col_w6}} | {sp_rss_str:>{col_w7}} | {spdup_str:>{col_w8}}"
        )

    print(
        f"{'-' * col_w1}-+-{'-' * col_w2}-+-{'-' * col_w3}-+-{'-' * col_w4}-+-{'-' * col_w5}-+-{'-' * col_w6}-+-{'-' * col_w7}-+-{'-' * col_w8}"
    )
    geomean_speedup = calculate_geomean(speedups)
    geomean_str = f"{geomean_speedup:.2f}x" if geomean_speedup > 0 else "N/A"
    print(
        f"{BOLD}{'Geometric Mean':<{col_w1}} | {'--':>{col_w2}} | {'--':>{col_w3}} | {'--':>{col_w4}} | {'--':>{col_w5}} | {'--':>{col_w6}} | {'--':>{col_w7}} | {geomean_str:>{col_w8}}{RESET}"
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
        r"\small",
        r"\caption{Comprehensive Multi-Metric Performance and Memory Evaluation: SimRV vs. Spike}",
        r"\label{tab:simrv_benchmark_results}",
        r"\begin{tabular}{lrrrrrrr}",
        r"\toprule",
        r"\textbf{Benchmark} & \textbf{Instructions} & \textbf{SimRV (s)} & \textbf{Spike (s)} & \textbf{SimRV (MIPS)} & \textbf{SimRV RSS} & \textbf{Spike RSS} & \textbf{Speedup} \\",
        r"\midrule",
    ]

    for res in suite_results:
        name = res["test_name"].replace("_", r"\_")
        insts = format_instrs(res["instructions"])
        s_time = res["simrv"]["stats"]["time"]["mean"]
        sp_time = res["spike"]["stats"]["time"]["mean"]
        s_mips = res["simrv"]["stats"]["wall_speed"]["mean"] / 1000.0
        s_rss = res["simrv"]["stats"].get("rss", {}).get("mean", 0.0)
        sp_rss = res["spike"]["stats"].get("rss", {}).get("mean", 0.0)

        if s_time > 0 and sp_time > 0:
            spdup_val = sp_time / s_time
            speedups.append(spdup_val)
            spdup_str = f"\\textbf{{{spdup_val:.2f}\\times}}"
        else:
            spdup_str = r"\text{N/A}"

        s_time_str = f"{s_time:.4f}"
        sp_time_str = f"{sp_time:.4f}" if sp_time > 0 else r"\text{N/A}"
        s_rss_str = f"{s_rss:.1f}\\,MB" if s_rss > 0 else "--"
        sp_rss_str = f"{sp_rss:.1f}\\,MB" if sp_rss > 0 else "--"

        lines.append(
            f"{name} & {insts} & {s_time_str} & {sp_time_str} & {s_mips:.2f} & {s_rss_str} & {sp_rss_str} & {spdup_str} \\\\"
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
            f"\\textbf{{Geometric Mean}} & -- & -- & -- & -- & -- & -- & {geomean_str} \\\\",
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
        "| Benchmark | Instructions | SimRV Time (s) | Spike Time (s) | SimRV Speed (MIPS) | SimRV RSS | Spike RSS | Speedup |",
        "| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]

    for res in suite_results:
        name = res["test_name"]
        insts = format_instrs(res["instructions"])
        s_time = res["simrv"]["stats"]["time"]["mean"]
        s_ci = res["simrv"]["stats"]["time"]["ci95"]
        sp_time = res["spike"]["stats"]["time"]["mean"]
        sp_ci = res["spike"]["stats"]["time"]["ci95"]
        s_mips = res["simrv"]["stats"]["wall_speed"]["mean"] / 1000.0
        s_rss = res["simrv"]["stats"].get("rss", {}).get("mean", 0.0)
        sp_rss = res["spike"]["stats"].get("rss", {}).get("mean", 0.0)

        if s_time > 0 and sp_time > 0:
            spdup_val = sp_time / s_time
            speedups.append(spdup_val)
            spdup_str = f"**{spdup_val:.2f}x**"
        else:
            spdup_str = "N/A"

        s_time_str = f"{s_time:.4f} ± {s_ci:.4f}" if s_ci > 0 else f"{s_time:.4f}"
        sp_time_str = f"{sp_time:.4f} ± {sp_ci:.4f}" if sp_time > 0 else "N/A"
        s_rss_str = f"{s_rss:.1f} MB" if s_rss > 0 else "--"
        sp_rss_str = f"{sp_rss:.1f} MB" if sp_rss > 0 else "--"

        lines.append(
            f"| `{name}` | {insts} | **{s_time_str}** | {sp_time_str} | **{s_mips:.2f} MIPS** | {s_rss_str} | {sp_rss_str} | {spdup_str} |"
        )

    geomean_speedup = calculate_geomean(speedups)
    geomean_str = f"**{geomean_speedup:.2f}x**" if geomean_speedup > 0 else "N/A"
    lines.append(
        f"| **Geometric Mean** | **--** | **--** | **--** | **--** | **--** | **--** | {geomean_str} |"
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
                "SimRV_Time_CI95_s",
                "Spike_Time_Mean_s",
                "Spike_Time_StdDev_s",
                "Spike_Time_CI95_s",
                "SimRV_Wall_KIPS",
                "SimRV_MIPS",
                "Spike_Wall_KIPS",
                "SimRV_Peak_RSS_MB",
                "Spike_Peak_RSS_MB",
                "Speedup_Ratio",
            ]
        )

        for res in suite_results:
            name = res["test_name"]
            insts = res["instructions"] or 0
            s_time = res["simrv"]["stats"]["time"]["mean"]
            s_time_sd = res["simrv"]["stats"]["time"]["stddev"]
            s_time_ci = res["simrv"]["stats"]["time"]["ci95"]
            sp_time = res["spike"]["stats"]["time"]["mean"]
            sp_time_sd = res["spike"]["stats"]["time"]["stddev"]
            sp_time_ci = res["spike"]["stats"]["time"]["ci95"]
            s_kips = res["simrv"]["stats"]["wall_speed"]["mean"]
            sp_kips = res["spike"]["stats"]["wall_speed"]["mean"]
            s_rss = res["simrv"]["stats"].get("rss", {}).get("mean", 0.0)
            sp_rss = res["spike"]["stats"].get("rss", {}).get("mean", 0.0)
            spdup = sp_time / s_time if s_time > 0 and sp_time > 0 else 0.0

            writer.writerow(
                [
                    name,
                    insts,
                    f"{s_time:.6f}",
                    f"{s_time_sd:.6f}",
                    f"{s_time_ci:.6f}",
                    f"{sp_time:.6f}",
                    f"{sp_time_sd:.6f}",
                    f"{sp_time_ci:.6f}",
                    f"{s_kips:.2f}",
                    f"{s_kips / 1000.0:.2f}",
                    f"{sp_kips:.2f}",
                    f"{s_rss:.2f}",
                    f"{sp_rss:.2f}",
                    f"{spdup:.4f}",
                ]
            )
    print(f"CSV report written to: {filepath}")



def aggregate_perf_counters(runs: list) -> dict:
    """Aggregate per-run perf stat counter dicts into mean/min/max per event."""
    if not runs:
        return {}
    result = {}
    all_events = {k for r in runs for k in r}
    for event in sorted(all_events):
        vals = [
            r[event]["value"]
            for r in runs
            if event in r and isinstance(r[event].get("value"), (int, float))
        ]
        if not vals:
            continue
        result[event] = {
            "mean": statistics.mean(vals),
            "min": min(vals),
            "max": max(vals),
            "n": len(vals),
        }
    return result


def check_stopping_policy_match(simrv_args, spike_args):
    """Return (simrv_limit, spike_limit, mismatch_bool).

    Emits a WARNING to stderr when both engines are given explicit instruction
    limits that differ, which would make wall-time comparisons invalid.
    """
    simrv_limit = None
    args_list = list(simrv_args)
    for i, a in enumerate(args_list):
        if a in ("-e", "--steps", "-s") and i + 1 < len(args_list):
            try:
                simrv_limit = int(args_list[i + 1])
            except ValueError:
                pass
    spike_limit = None
    for a in (spike_args or []):
        if a.startswith("--instructions="):
            try:
                spike_limit = int(a.split("=", 1)[1])
            except ValueError:
                pass
    mismatch = (
        simrv_limit is not None
        and spike_limit is not None
        and simrv_limit != spike_limit
    )
    if mismatch:
        print(
            f"WARNING: stopping-policy mismatch — SimRV -e {simrv_limit:,}, "
            f"Spike --instructions={spike_limit:,}. "
            "Wall-time comparison is not meaningful.",
            file=sys.stderr,
        )
    return simrv_limit, spike_limit, mismatch


def parse_event_pipe_data(raw: bytes) -> dict:
    """Parse SIMRV_EVENT_FD pipe bytes into a dict of {name: value}.

    Each line has the format: ``<name> <ns_timestamp> <value>``
    Returns the *last* value seen for each event name (later events overwrite).
    """
    events: dict = {}
    for line in raw.decode("utf-8", errors="ignore").splitlines():
        parts = line.split()
        if len(parts) == 3:
            name, _ts, raw_val = parts
            try:
                events[name] = int(raw_val)
            except ValueError:
                pass
    return events


def simrv_benchmark_command(simrv_bin, simrv_args, elf_path, limit, tohost_addr, isa):
    command = [simrv_bin, "--cli", *simrv_args, "--isa", isa, "-m", elf_path, "-b", "-H", tohost_addr]
    if limit > 0:
        command.extend(["-e", str(limit)])
    return command


def spike_benchmark_command(spike_bin, elf_path, limit, isa):
    command = [spike_bin, f"--isa={isa}"]
    if limit > 0:
        command.append(f"--instructions={limit}")
    command.append(elf_path)
    return command


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
    simrv_args,
    perf_bin,
    perf_events,
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
            os.path.join(riscv_tests_dir, "..", "coremark", f"{test_target}.riscv"),
            os.path.join(riscv_tests_dir, "..", "coremark", f"{test_target}64.riscv"),
            os.path.join(riscv_tests_dir, "coremark", f"{test_target}.riscv"),
            os.path.join(riscv_tests_dir, "coremark", f"{test_target}64.riscv"),
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
    except (OSError, subprocess.CalledProcessError) as e:
        print(f"ERROR: objcopy conversion failed: {e}", file=sys.stderr)
        return None

    tohost_addr = (
        tohost_arg
        or resolve_tohost(elf_path, nm_tool)
        or "0x80001000"
    )
    # The caller may be comparing compiler builds or an out-of-tree binary.  Do not replace an
    # explicit --simrv selection with the in-tree default based on the guest ELF's XLEN.
    xlen = detect_xlen(simrv_bin, elf_path)
    # G implies Zicsr and Zifencei; counters are named explicitly in the ISA string.
    isa = isa_override or f"rv{xlen}gc_zicntr"

    simrv_times = []
    simrv_sim_times = []
    simrv_speeds = []
    simrv_rss = []
    simrv_host_counters = []
    simrv_instrs = None
    simrv_cycles = None
    simrv_event_data: list[dict] = []  # per-run SIMRV_EVENT_FD pipe payloads

    has_time_wrapper = os.path.isfile("/usr/bin/time") and os.access(
        "/usr/bin/time", os.X_OK
    )

    print(f"\n--- Running Benchmark Target: {test_basename} ---")
    print(f"  SimRV binary : {simrv_bin} (RV{xlen})")
    print(f"  ELF path     : {elf_path}")
    print(f"  Tohost addr  : {tohost_addr}")
    print(f"  ISA contract : {isa}")
    print(f"  Stop policy  : {'full guest completion' if limit == 0 else f'{limit:,} instructions'}")

    simrv_base_cmd = simrv_benchmark_command(
        simrv_bin, simrv_args, elf_path, limit, tohost_addr, isa
    )
    spike_available = spike_bin and (is_executable(spike_bin) or which(spike_bin))
    spike_base_cmd = (
        spike_benchmark_command(spike_bin, elf_path, limit, isa)
        if spike_available
        else None
    )

    # Warn immediately if the two engines will use different stopping limits.
    _simrv_limit, _spike_limit, stopping_policy_mismatch = check_stopping_policy_match(
        simrv_base_cmd, spike_base_cmd
    )

    for _ in range(warmups):
        warmup = subprocess.run(
            simrv_base_cmd,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=timeout,
            stdin=subprocess.DEVNULL,
            check=False,
        )
        if warmup.returncode != 0:
            print(f"  [ERROR] SimRV warmup failed on {test_basename}", file=sys.stderr)
            return None
        if spike_base_cmd:
            spike_warmup = subprocess.run(
                spike_base_cmd,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE,
                timeout=timeout,
                stdin=subprocess.DEVNULL,
                check=False,
            )
            if spike_warmup.returncode != 0:
                detail = spike_warmup.stderr.decode("utf-8", errors="ignore").strip()
                print(
                    f"  [ERROR] Spike preflight failed on {test_basename} "
                    f"(code {spike_warmup.returncode}): {detail}",
                    file=sys.stderr,
                )
                return None

    # SimRV runs — use Popen so we can open the SIMRV_EVENT_FD benchmark pipe.
    import threading
    for i in range(1, runs + 1):
        log_file = os.path.join(log_dir, f"bench_simrv_{test_basename}_{i}.log")
        simrv_cmd = list(simrv_base_cmd)
        perf_output = os.path.join(log_dir, f"bench_simrv_{test_basename}_{i}.perf.csv")
        simrv_cmd = wrap_measured_command(
            simrv_cmd, perf_bin, perf_events, perf_output, has_time_wrapper
        )

        event_r, event_w = os.pipe()
        run_env = {**os.environ, "SIMRV_EVENT_FD": str(event_w)}

        # Drain the pipe read end in a background thread to avoid blocking the writer.
        event_chunks: list[bytes] = []

        def _drain_pipe(fd: int, chunks: list) -> None:
            try:
                while True:
                    chunk = os.read(fd, 4096)
                    if not chunk:
                        break
                    chunks.append(chunk)
            except OSError:
                pass
            finally:
                try:
                    os.close(fd)
                except OSError:
                    pass

        drain_thread = threading.Thread(
            target=_drain_pipe, args=(event_r, event_chunks), daemon=True
        )
        drain_thread.start()

        start_t = time.perf_counter()
        proc = None
        try:
            proc = subprocess.Popen(
                simrv_cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                stdin=subprocess.DEVNULL,
                env=run_env,
                pass_fds=(event_w,),
            )
            os.close(event_w)  # write end belongs to the child; close our copy
            event_w = -1
            try:
                stdout_bytes, stderr_bytes = proc.communicate(timeout=timeout)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.communicate()
                drain_thread.join(timeout=2)
                print(
                    f"  [ERROR] SimRV timed out on {test_basename} (iter {i})",
                    file=sys.stderr,
                )
                return None
            end_t = time.perf_counter()
            drain_thread.join(timeout=2)

            stdout_str = stdout_bytes.decode("utf-8", errors="ignore")
            stderr_str = stderr_bytes.decode("utf-8", errors="ignore")

            with open(log_file, "w") as lf:
                lf.write(stdout_str)
                lf.write(stderr_str)

            if proc.returncode != 0:
                print(
                    f"  [ERROR] SimRV failed on {test_basename} (code {proc.returncode}). Log: {log_file}",
                    file=sys.stderr,
                )
                return None

            insts, cycles, kips, sim_time = parse_simrv_output(
                stdout_str + "\n" + stderr_str
            )
            if kips is None:
                print(
                    f"  [ERROR] Could not parse performance speed from SimRV output. Log: {log_file}",
                    file=sys.stderr,
                )
                return None

            rss_match = re.search(r"__MAX_RSS_KB__:(\d+)", stderr_str)
            if rss_match:
                simrv_rss.append(int(rss_match.group(1)) / 1024.0)
            if perf_bin:
                simrv_host_counters.append(parse_perf_stat(perf_output))

            raw_pipe = b"".join(event_chunks)
            if raw_pipe:
                simrv_event_data.append(parse_event_pipe_data(raw_pipe))

            elapsed = end_t - start_t
            simrv_times.append(elapsed)
            simrv_speeds.append(kips)
            simrv_sim_times.append(sim_time if sim_time is not None else 0.0)
            if insts:
                simrv_instrs = insts
            if cycles:
                simrv_cycles = cycles
        except subprocess.TimeoutExpired:
            print(
                f"  [ERROR] SimRV timed out on {test_basename} (iter {i})",
                file=sys.stderr,
            )
            return None
        except OSError as error:
            print(
                f"  [ERROR] Could not launch SimRV on {test_basename} (iter {i}): {error}",
                file=sys.stderr,
            )
            return None
        finally:
            if event_w >= 0:
                try:
                    os.close(event_w)
                except OSError:
                    pass
            if proc is not None and proc.poll() is None:
                proc.kill()
                proc.communicate()
            drain_thread.join(timeout=2)

    # Spike runs
    spike_times = []
    spike_speeds = []
    spike_rss = []
    spike_host_counters = []
    if spike_available:
        for i in range(1, runs + 1):
            spike_cmd = list(spike_base_cmd)
            perf_output = os.path.join(log_dir, f"bench_spike_{test_basename}_{i}.perf.csv")
            spike_cmd = wrap_measured_command(
                spike_cmd, perf_bin, perf_events, perf_output, has_time_wrapper
            )

            start_t = time.perf_counter()
            try:
                res = subprocess.run(
                    spike_cmd,
                    capture_output=True,
                    timeout=timeout,
                    stdin=subprocess.DEVNULL,
                    check=False,
                )
                end_t = time.perf_counter()
                if res.returncode != 0:
                    err_msg = res.stderr.decode("utf-8", errors="ignore").strip()
                    print(
                        f"  [ERROR] Spike failed on {test_basename} (code {res.returncode}): {err_msg}",
                        file=sys.stderr,
                    )
                    return None

                stderr_str = res.stderr.decode("utf-8", errors="ignore")
                rss_match = re.search(r"__MAX_RSS_KB__:(\d+)", stderr_str)
                if rss_match:
                    spike_rss.append(int(rss_match.group(1)) / 1024.0)
                if perf_bin:
                    spike_host_counters.append(parse_perf_stat(perf_output))

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
        "rss": calculate_stats(simrv_rss),
        "overhead": calculate_stats(
            [wall - sim for wall, sim in zip(simrv_times, simrv_sim_times)]
        ),
    }

    spike_stats = {
        "time": calculate_stats(spike_times),
        "wall_speed": calculate_stats(spike_speeds),
        "rss": calculate_stats(spike_rss),
    }

    # Derive decode-cache hit rate from last run's event pipe (basis points → fraction).
    decode_cache_hit_rate: float | None = None
    if simrv_event_data:
        last_ev = simrv_event_data[-1]
        if "decode_cache_hit_rate" in last_ev:
            decode_cache_hit_rate = last_ev["decode_cache_hit_rate"] / 10000.0

    configuration = {"xlen": xlen, "isa": isa, "simrv_args": list(simrv_args),
                     "instruction_limit": limit, "tohost": tohost_addr}
    provenance = experiment_provenance(
        root=root_dir, simrv=simrv_bin, spike=spike_bin if spike_available else None,
        workload=elf_path, configuration=configuration, simrv_command=simrv_base_cmd,
        spike_command=spike_base_cmd if spike_available else None,
    )

    return {
        "schema_version": 1,
        "test_name": test_basename,
        "workload_path": os.path.realpath(elf_path),
        "workload_sha256": sha256_file(elf_path),
        "runs": runs,
        "warmups": warmups,
        "instruction_limit": limit,
        "instructions": simrv_instrs,
        "simulated_cycles": simrv_cycles,
        "xlen": xlen,
        "isa": isa,
        "spike_isa": isa,
        "configuration_fingerprint": provenance["configuration_fingerprint"],
        "provenance": provenance,
        "simrv_args": list(simrv_args),
        "simrv_binary_sha256": sha256_file(simrv_bin),
        "simrv_version": command_version(simrv_bin),       # kept for backwards compat
        "simulator_version": command_version(simrv_bin),   # canonical name
        "spike_version": command_version(spike_bin) if spike_available and spike_bin else None,
        "simrv_revision": repository_revision(root_dir),
        "compiler": command_version(os.environ.get("CXX", "c++")),
        "host": platform.platform(),
        "stopping_policy_mismatch": stopping_policy_mismatch,
        "simrv": {
            "runs_time": simrv_times,
            "runs_sim_speed_kips": simrv_speeds,
            "runs_wall_speed_kips": simrv_wall_speeds,
            "runs_rss_mb": simrv_rss,
            "host_counters": simrv_host_counters,
            "host_perf": aggregate_perf_counters(simrv_host_counters),
            "decode_cache_hit_rate": decode_cache_hit_rate,
            "stats": simrv_stats,
        },
        "spike": {
            "runs_time": spike_times,
            "runs_wall_speed_kips": spike_speeds,
            "runs_rss_mb": spike_rss,
            "host_counters": spike_host_counters,
            "host_perf": aggregate_perf_counters(spike_host_counters),
            "stats": spike_stats,
        },
    }


def load_compare_results(path: pathlib.Path | str) -> dict[str, float]:
    p = pathlib.Path(path)
    report = json.loads(p.read_text(encoding="utf-8"))
    if "results" in report:
        return {f"rv{row['xlen']}:{row['workload']}": row["median_kips"]
                for row in report["results"] if row["median_kips"] > 0}
    results = report.get("suite_results", [report])
    speeds = {}
    for result in results:
        speed = result["simrv"]["stats"]["wall_speed"]["median"]
        if speed > 0:
            speeds[f"rv{result['xlen']}:{result['test_name']}"] = speed
    return speeds

load_results = load_compare_results


def compare_main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Compare SimRV benchmark evidence")
    parser.add_argument("baseline", type=pathlib.Path)
    parser.add_argument("candidate", type=pathlib.Path)
    parser.add_argument("--minimum-geomean", type=float, default=5.0)
    parser.add_argument("--maximum-regression", type=float, default=3.0)
    parser.add_argument("--json", type=pathlib.Path)
    parser.add_argument("--enforce", action="store_true", help="Return nonzero when supplied thresholds are exceeded")
    args = parser.parse_args(argv)

    # Warn when either report was produced with a stopping-policy mismatch.
    for label, path in (("baseline", args.baseline), ("candidate", args.candidate)):
        try:
            raw = json.loads(path.read_text(encoding="utf-8"))
            results = raw.get("suite_results", [raw])
            for res in results:
                if res.get("stopping_policy_mismatch"):
                    print(
                        f"WARNING: {label} '{path.name}' contains a stopping-policy mismatch "
                        f"for '{res.get('test_name', '?')}'. "
                        "Wall-time comparison may be invalid.",
                        file=sys.stderr,
                    )
        except (OSError, json.JSONDecodeError):
            continue

    baseline = load_compare_results(args.baseline)
    candidate = load_compare_results(args.candidate)
    names = sorted(baseline.keys() & candidate.keys())
    if not names or set(baseline) != set(candidate):
        missing = sorted(set(baseline) ^ set(candidate))
        print(f"benchmark sets differ; unmatched: {missing}", file=sys.stderr)
        return 2

    ratios = []
    failed = False
    for name in names:
        change = (candidate[name] / baseline[name] - 1.0) * 100.0
        ratios.append(candidate[name] / baseline[name])
        print(f"{name}: {change:+.2f}%")
        if change < -args.maximum_regression:
            print(f"  regression exceeds {args.maximum_regression:.2f}%", file=sys.stderr)
            failed = True

    geomean = (math.exp(sum(math.log(value) for value in ratios) / len(ratios)) - 1.0) * 100.0
    print(f"geometric-mean change: {geomean:+.2f}%")
    if geomean < args.minimum_geomean:
        print(f"geometric mean is below {args.minimum_geomean:.2f}%", file=sys.stderr)
        failed = True
    if failed and not args.enforce:
        print("threshold observations are informational (evidence-only policy)")
    if args.json:
        report = {"schema_version": 1, "minimum_geomean_percent": args.minimum_geomean,
                  "maximum_regression_percent": args.maximum_regression,
                  "geomean_change_percent": geomean, "passed": not failed,
                  "results": [{"benchmark": name, "baseline_kips": baseline[name],
                               "candidate_kips": candidate[name],
                               "change_percent": (candidate[name] / baseline[name] - 1.0) * 100.0}
                              for name in names]}
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 1 if failed and args.enforce else 0


def bootstrap_median_ci(samples: list[float], resamples: int = 10_000,
                        confidence: float = 0.95, seed: str = "simrv") -> list[float]:
    if not samples:
        return [0.0, 0.0]
    if len(samples) == 1:
        return [samples[0], samples[0]]
    generator = random.Random(int(hashlib.sha256(seed.encode()).hexdigest()[:16], 16))
    medians = sorted(statistics.median(generator.choices(samples, k=len(samples)))
                     for _ in range(resamples))
    tail = (1.0 - confidence) / 2.0
    return [medians[round(tail * (resamples - 1))],
            medians[round((1.0 - tail) * (resamples - 1))]]


def load_aggregate_rows(paths: list[pathlib.Path | str], resamples: int = 10_000,
                        confidence: float = 0.95) -> list[dict]:
    rows = []
    for p in sorted(paths):
        path = pathlib.Path(p)
        report = json.loads(path.read_text(encoding="utf-8"))
        for result in report.get("suite_results", [report]):
            speeds = result["simrv"].get("runs_wall_speed_kips", [])
            spike = result.get("spike", {}).get("runs_wall_speed_kips", [])
            median_simrv = statistics.median(speeds) if speeds else 0.0
            median_spike = statistics.median(spike) if spike else 0.0
            fingerprint = result.get("configuration_fingerprint", "unknown")
            rows.append({"source": path.name, "configuration_fingerprint": fingerprint,
                         "xlen": result["xlen"], "workload": path.stem,
                         "target": result["test_name"], "samples": len(speeds),
                         "median_kips": median_simrv,
                         "median_kips_ci": bootstrap_median_ci(
                             speeds, resamples, confidence, f"{fingerprint}:{path.stem}:simrv"),
                         "cv_percent": result["simrv"].get("stats", {}).get("wall_speed", {}).get("cv", 0.0),
                         "spike_median_kips": median_spike,
                         "spike_median_kips_ci": bootstrap_median_ci(
                             spike, resamples, confidence, f"{fingerprint}:{path.stem}:spike"),
                         "simrv_over_spike": median_simrv / median_spike if median_spike else None})
    return sorted(rows, key=lambda row: (row["xlen"], row["workload"], row["source"]))

load_rows = load_aggregate_rows


def aggregate_main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Deterministically aggregate benchmark JSON")
    parser.add_argument("inputs", nargs="+", type=pathlib.Path)
    parser.add_argument("--json", type=pathlib.Path, required=True)
    parser.add_argument("--table", type=pathlib.Path, required=True)
    parser.add_argument("--plot", type=pathlib.Path, required=True)
    parser.add_argument("--bootstrap-resamples", type=int, default=10_000)
    parser.add_argument("--confidence", type=float, default=0.95)
    args = parser.parse_args(argv)
    if args.bootstrap_resamples < 1 or not 0.0 < args.confidence < 1.0:
        parser.error("bootstrap resamples must be positive and confidence must be between 0 and 1")

    rows = load_aggregate_rows(args.inputs, args.bootstrap_resamples, args.confidence)
    aggregate = {"schema_version": 1, "statistic": "median", "unit": "KIPS",
                 "confidence": args.confidence, "bootstrap_resamples": args.bootstrap_resamples,
                 "results": rows}
    for path in (args.json, args.table, args.plot):
        path.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps(aggregate, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    lines = ["| XLEN | Workload | N | SimRV median KIPS (CI) | CV | Spike median KIPS | SimRV/Spike |",
             "| ---: | --- | ---: | ---: | ---: | ---: | ---: |"]
    for row in rows:
        low, high = row["median_kips_ci"]
        ratio = f"{row['simrv_over_spike']:.3f}×" if row["simrv_over_spike"] else "N/A"
        lines.append(f"| {row['xlen']} | {row['workload']} | {row['samples']} | "
                     f"{row['median_kips']:.3f} [{low:.3f}, {high:.3f}] | "
                     f"{row['cv_percent']:.2f}% | {row['spike_median_kips']:.3f} | {ratio} |")
    args.table.write_text("\n".join(lines) + "\n", encoding="utf-8")
    width, row_height = 760, 26
    maximum = max(1.0, max((max(r["median_kips"], r["spike_median_kips"])
                            for r in rows), default=0.0))
    svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{50 + row_height * len(rows)}" viewBox="0 0 {width} {50 + row_height * len(rows)}">',
           '<style>text{font:12px sans-serif}.title{font:bold 15px sans-serif}.simrv{fill:#4c78a8}.spike{fill:#f58518}</style>',
           '<text class="title" x="10" y="20">Median throughput: SimRV (blue) vs Spike (orange)</text>']
    for index, row in enumerate(rows):
        y = 42 + index * row_height
        label = html.escape(f"RV{row['xlen']} {row['workload']}")
        bar = 450 * row["median_kips"] / maximum
        spike_bar = 450 * row["spike_median_kips"] / maximum
        svg.extend([f'<text x="10" y="{y + 12}">{label}</text>',
                    f'<rect class="simrv" x="210" y="{y}" width="{bar:.2f}" height="8"/>',
                    f'<rect class="spike" x="210" y="{y + 9}" width="{spike_bar:.2f}" height="8"/>'])
    svg.append("</svg>")
    args.plot.write_text("\n".join(svg) + "\n", encoding="utf-8")
    return 0


def gdb_main(argv: list[str]) -> int:
    import selectors
    import socket
    import tempfile

    def run_gdb_iter(binary, guest, instructions, debugger, trace=None):
        command = ["stdbuf", "-oL", binary, "--cli", "--mode", "fast", "-m", str(guest),
                   "--ram-size", "33554432", "--net", "none", "-e", str(instructions)]
        if debugger:
            command += ["--gdb", "--gdb-port", "0"]
        if trace:
            command = ["strace", "-ff", "-o", trace, "-e", "trace=network,poll,read,write"] + command
        started = time.perf_counter()
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT)
        client = None
        try:
            if debugger:
                selector = selectors.DefaultSelector()
                selector.register(process.stdout, selectors.EVENT_READ)
                output = b""
                deadline = time.monotonic() + 10
                while not (match := re.search(rb"GDB server listening on port (\d+)", output)):
                    if not selector.select(max(0, deadline - time.monotonic())):
                        raise RuntimeError("GDB listener readiness timed out")
                    chunk = os.read(process.stdout.fileno(), 8192)
                    if not chunk:
                        raise RuntimeError(output.decode(errors="replace"))
                    output += chunk
                selector.close()
                client = socket.create_connection(("127.0.0.1", int(match[1])), timeout=5)
                client.sendall(b"$c#63")
            output, _ = process.communicate(timeout=60)
            if process.returncode:
                raise RuntimeError(output.decode(errors="replace"))
            return instructions / (time.perf_counter() - started)
        finally:
            if client:
                client.close()
            if process.poll() is None:
                process.kill()
                process.wait()

    parser = argparse.ArgumentParser(description="Compare paired CLI/GDB runs")
    parser.add_argument("--simrv", default="build/rv64-release/simrv")
    parser.add_argument("--instructions", type=int, default=100_000_000)
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--json", type=pathlib.Path)
    parser.add_argument("--trace-prefix", help="Record a separate short GDB syscall trace")
    args = parser.parse_args(argv)
    if hasattr(os, "sched_getaffinity"):
        os.sched_setaffinity(0, {min(os.sched_getaffinity(0))})
    with tempfile.TemporaryDirectory(prefix="simrv-gdb-bench-") as directory:
        guest = pathlib.Path(directory) / "loop.bin"
        guest.write_bytes(bytes.fromhex("938010006ff0dfff"))
        samples = {"cli": [], "gdb": []}
        for iteration in range(args.runs):
            for name in (("cli", "gdb") if iteration % 2 == 0 else ("gdb", "cli")):
                samples[name].append(run_gdb_iter(args.simrv, guest, args.instructions, name == "gdb"))
        medians = {name: statistics.median(values) for name, values in samples.items()}
        regression = 100 * (1 - medians["gdb"] / medians["cli"])
        result = {"instructions": args.instructions, "runs": args.runs,
                  "instructions_per_second": samples, "medians": medians,
                  "regression_percent": regression, "passes_5_percent_limit": regression <= 5}
        print(json.dumps(result, indent=2))
        if args.json:
            args.json.write_text(json.dumps(result, indent=2) + "\n")
        if args.trace_prefix:
            run_gdb_iter(args.simrv, guest, 1_000_000, True, args.trace_prefix)
        return 0 if regression <= 5 else 1


def main():
    if len(sys.argv) > 1 and sys.argv[1] in ("compare", "aggregate", "gdb"):
        sub = sys.argv[1]
        if sub == "compare":
            sys.exit(compare_main(sys.argv[2:]))
        elif sub == "aggregate":
            sys.exit(aggregate_main(sys.argv[2:]))
        elif sub == "gdb":
            sys.exit(gdb_main(sys.argv[2:]))

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
        choices=["realworld"],
        help="Run the real-world benchmark suite",
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
        default=0,
        help="Symmetric instruction cap for SimRV and Spike (default: 0, run to completion)",
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
    parser.add_argument(
        "--simrv-arg",
        action="append",
        default=[],
        help="Additional argument forwarded to every SimRV invocation (repeatable)",
    )
    parser.add_argument(
        "--compare-instruction",
        action="store_true",
        help="Also run the instruction engine and record the CA host-time ratio",
    )
    parser.add_argument(
        "--perf",
        action="store_true",
        help="Record supplemental Linux perf host counters for measured runs",
    )
    parser.add_argument(
        "--perf-events",
        default="task-clock,cycles,instructions,branches,branch-misses,cache-references,cache-misses",
        help="Comma-separated perf events used with --perf",
    )
    parser.add_argument(
        "--cv-gate",
        type=float,
        default=5.0,
        metavar="PCT",
        help="Print a WARNING when wall-time CV exceeds PCT%% (default: 5.0). "
             "Use --enforce-cv to make this a hard failure.",
    )
    parser.add_argument(
        "--enforce-cv",
        action="store_true",
        help="Exit with code 1 when any result exceeds --cv-gate",
    )

    args = parser.parse_args()

    if args.list_benchmarks:
        print("Available Real-World Benchmarks:")
        for bm in REALWORLD_BENCHMARKS:
            print(f"  - {bm}")
        sys.exit(0)

    if args.runs <= 0:
        print("ERROR: --runs must be a positive integer", file=sys.stderr)
        sys.exit(1)
    if args.warmups < 0:
        print("ERROR: --warmups cannot be negative", file=sys.stderr)
        sys.exit(1)
    perf_bin = which("perf") if args.perf else None
    if args.perf and not perf_bin:
        print("ERROR: --perf requested but perf is not executable", file=sys.stderr)
        sys.exit(2)

    script_dir = os.path.dirname(os.path.abspath(__file__))
    root_dir = os.path.dirname(script_dir)

    # 1. Resolve riscv-tests directory from an explicit argument or environment.
    riscv_tests_dir = args.riscv_tests_dir or os.environ.get("RISCV_TESTS_DIR")
    if not riscv_tests_dir and not args.test:
        print("ERROR: Set --riscv-tests-dir or RISCV_TESTS_DIR when using benchmark names", file=sys.stderr)
        sys.exit(2)

    if riscv_tests_dir and os.path.isdir(os.path.join(riscv_tests_dir, "share", "riscv-tests")):
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
        simrv_64 = os.path.join(root_dir, "build/rv64-release/simrv")
        simrv_32 = os.path.join(root_dir, "build/rv32-release/simrv")
        if is_executable(simrv_64):
            simrv_bin = simrv_64
        elif is_executable(simrv_32):
            simrv_bin = simrv_32
        elif which("simrv"):
            simrv_bin = which("simrv")
        elif is_executable(os.path.join(os.path.dirname(os.path.abspath(__file__)), "simrv")):
            simrv_bin = os.path.join(os.path.dirname(os.path.abspath(__file__)), "simrv")
        else:
            simrv_bin = os.path.join(root_dir, "simrv")

    if not is_executable(simrv_bin):
        print(
            f"ERROR: SimRV binary not executable: {simrv_bin}", file=sys.stderr
        )
        sys.exit(1)

    # Determine targets to benchmark
    targets = []
    if args.suite == "realworld":
        targets = REALWORLD_BENCHMARKS
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
            args.simrv_arg,
            perf_bin,
            args.perf_events,
        )
        if result:
            if args.compare_instruction:
                baseline = run_benchmark_single(
                    target,
                    simrv_bin,
                    None,
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
                    ["--mode", "fast"],
                    perf_bin,
                    args.perf_events,
                )
                if baseline:
                    result["instruction_baseline"] = baseline["simrv"]
                    instruction_median = baseline["simrv"]["stats"]["time"]["median"]
                    cycle_median = result["simrv"]["stats"]["time"]["median"]
                    result["ca_to_instruction_time_ratio"] = (
                        cycle_median / instruction_median if instruction_median > 0 else None
                    )
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

    # CV gate: check wall-time coefficient of variation across all results.
    cv_failed = False
    for res in suite_results:
        cv = res.get("simrv", {}).get("stats", {}).get("time", {}).get("cv", 0.0)
        name = res.get("test_name", "?")
        if cv > args.cv_gate:
            print(
                f"WARNING: CV gate exceeded — {name} wall-time CV {cv:.2f}% > "
                f"{args.cv_gate:.1f}% threshold. "
                "Run on an idle host with a pinned CPU governor before profiling.",
                file=sys.stderr,
            )
            cv_failed = True
        else:
            print(f"  CV check passed: {name} wall-time CV {cv:.2f}% ≤ {args.cv_gate:.1f}%")

    # Summarise any stopping-policy mismatches detected during runs.
    mismatched = [r["test_name"] for r in suite_results if r.get("stopping_policy_mismatch")]
    if mismatched:
        print(
            f"WARNING: stopping-policy mismatch in {len(mismatched)} result(s): "
            f"{', '.join(mismatched)}. SimRV vs Spike comparisons are not meaningful.",
            file=sys.stderr,
        )

    if cv_failed and args.enforce_cv:
        sys.exit(1)


if __name__ == "__main__":
    main()
