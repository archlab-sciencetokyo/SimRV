#!/usr/bin/env python3
"""
SimRV Host Instruction Execution Micro-Benchmark.

Measures host execution speed (MIPS and ns/instruction) across guest instruction categories
using pre-built ISA micro-benchmarks in examples/isa/build-rv64/.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

SPEED_RE = re.compile(r"Simulation speed\s*:\s*([\d.]+)\s*(MIPS|KIPS)")
TOTAL_INSTS_RE = re.compile(r"Executed instructions\s*:\s*([\d.]+[KMG]?)\s*\(([0-9,]+)\)")
ELAPSED_CYCLES_RE = re.compile(r"Elapsed cycles \(clocks\)\s*:\s*([\d.]+[KMG]?)\s*\(([0-9,]+)\)")
CPI_RE = re.compile(r"Cycles Per Instr \(CPI\)\s*:\s*([\d.]+)")

BENCHMARKS = [
    {
        "category": "Integer ALU",
        "elf": "arithmetic.elf",
        "description": "ADD, SUB, AND, OR, XOR, SLL, SRL, SRA, ADDI, SLTI",
    },
    {
        "category": "Mul / Div",
        "elf": "multiply-divide.elf",
        "description": "MUL, MULH, MULHU, DIV, DIVU, REM, REMU",
    },
    {
        "category": "Memory (Load/Store)",
        "elf": "load-widths.elf",
        "description": "LB, LH, LW, LD, SB, SH, SW, SD",
    },
    {
        "category": "Control (Branch)",
        "elf": "branches.elf",
        "description": "BEQ, BNE, BLT, BGE, BLTU, BGEU",
    },
    {
        "category": "Control (Calls/Jumps)",
        "elf": "control-flow-calls.elf",
        "description": "JAL, JALR, function prologues and returns",
    },
    {
        "category": "Floating Point",
        "elf": "floating-point.elf",
        "description": "FADD.D, FSUB.D, FMUL.D, FDIV.D, FLD, FSD",
    },
    {
        "category": "RVC Compressed",
        "elf": "compressed.elf",
        "description": "C.ADDI, C.LI, C.MV, C.J, C.LW, C.SW",
    },
    {
        "category": "Loop & Array Traversal",
        "elf": "loops-arrays.elf",
        "description": "Sequential loop iteration and indexed memory access",
    },
]


def parse_instmix_report(report_path: Path):
    if not report_path.is_file():
        return {}
    results = {}
    in_category_summary = False
    with open(report_path, "r", encoding="utf-8") as f:
        for line in f:
            if "--- Category Summary ---" in line:
                in_category_summary = True
                continue
            if in_category_summary and ":" in line and "(" in line:
                parts = line.split(":")
                cat_name = parts[0].strip()
                rest = parts[1]
                # Format: Count (Share%) | Cycles (Cycle%) | Avg CPI
                subparts = rest.split("|")
                if len(subparts) >= 3:
                    count_str = subparts[0].split("(")[0].strip().replace(",", "")
                    cycles_str = subparts[1].split("(")[0].strip().replace(",", "")
                    cpi_str = subparts[2].strip()
                    try:
                        results[cat_name] = {
                            "count": int(count_str),
                            "cycles": int(cycles_str),
                            "cpi": float(cpi_str),
                        }
                    except ValueError:
                        pass
    return results


def run_benchmark(simrv_bin: str, elf_path: Path, steps: int, mode: str, runs: int):
    trace_dir = Path("/tmp/simrv-bench-trace")
    trace_dir.mkdir(parents=True, exist_ok=True)
    instmix_file = trace_dir / "instmix.txt"

    mips_samples = []
    ns_per_insn_samples = []
    guest_cpi = 1.0
    retired_insts = steps

    for _ in range(runs):
        if instmix_file.exists():
            instmix_file.unlink()

        cmd = [
            simrv_bin,
            "--cli",
            "-m",
            str(elf_path),
            "-s",
            str(steps),
            "--mode",
            mode,
            "--instmix",
            "--trace-dir",
            str(trace_dir),
        ]

        start_ns = time.perf_counter_ns()
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        end_ns = time.perf_counter_ns()

        if res.returncode != 0:
            return None

        elapsed_ns = max(1, end_ns - start_ns)
        output = res.stdout + res.stderr

        m_speed = SPEED_RE.search(output)
        if m_speed:
            val = float(m_speed.group(1))
            mips = val if m_speed.group(2) == "MIPS" else val / 1000.0
            mips_samples.append(mips)
        else:
            calc_mips = (steps / (elapsed_ns / 1e9)) / 1e6
            mips_samples.append(calc_mips)

        m_insts = TOTAL_INSTS_RE.search(output)
        if m_insts:
            retired_insts = int(m_insts.group(2).replace(",", ""))

        ns_per_insn = elapsed_ns / max(1, retired_insts)
        ns_per_insn_samples.append(ns_per_insn)

        m_cpi = CPI_RE.search(output)
        if m_cpi:
            guest_cpi = float(m_cpi.group(1))

    mix_stats = parse_instmix_report(instmix_file)

    avg_mips = sum(mips_samples) / len(mips_samples)
    avg_ns = sum(ns_per_insn_samples) / len(ns_per_insn_samples)

    return {
        "mips": avg_mips,
        "ns_per_insn": avg_ns,
        "guest_cpi": guest_cpi,
        "retired": retired_insts,
        "category_mix": mix_stats,
    }


def main():
    parser = argparse.ArgumentParser(
        description="Benchmark SimRV instruction execution performance across categories."
    )
    parser.add_argument(
        "--simrv",
        default="build/rv64-release/simrv",
        help="Path to simrv binary (default: build/rv64-release/simrv)",
    )
    parser.add_argument(
        "--examples-dir",
        default="examples/isa/build-rv64",
        help="Path to compiled ISA benchmark ELFs directory",
    )
    parser.add_argument(
        "--steps",
        type=int,
        default=200000,
        help="Instruction step limit per benchmark (default: 200,000)",
    )
    parser.add_argument(
        "--mode",
        choices=["fast", "detailed", "cycle-accurate"],
        default="fast",
        help="Simulation execution mode (default: fast)",
    )
    parser.add_argument(
        "--runs",
        type=int,
        default=3,
        help="Number of iterations to average per benchmark (default: 3)",
    )
    parser.add_argument("--json", action="store_true", help="Output results in JSON format")
    parser.add_argument("--markdown", action="store_true", help="Output results in Markdown format")

    args = parser.parse_args()

    simrv_bin = os.path.abspath(args.simrv)
    if not os.path.isfile(simrv_bin):
        sys.exit(f"Error: simrv binary not found at '{simrv_bin}'. Run 'cmake --build --preset rv64-release' first.")

    examples_dir = Path(args.examples_dir)
    if not examples_dir.is_dir():
        sys.exit(f"Error: ISA benchmarks directory not found at '{examples_dir}'.")

    results = []

    if not args.json:
        print(f"=========================================================================================")
        print(f"                     SimRV Host Instruction Micro-Benchmarks                             ")
        print(f"=========================================================================================")
        print(f" Binary:   {simrv_bin}")
        print(f" Mode:     {args.mode} | Steps: {args.steps:,} | Iterations: {args.runs}")
        print(f"=========================================================================================\n")
        print(f" {'Category':<24} | {'Host Speed':>12} | {'Host Latency':>14} | {'Guest CPI':>9} | Description")
        print(f"-----------------------------------------------------------------------------------------")

    for bench in BENCHMARKS:
        elf_path = examples_dir / bench["elf"]
        if not elf_path.is_file():
            continue

        res = run_benchmark(simrv_bin, elf_path, args.steps, args.mode, args.runs)
        if res is None:
            continue

        bench_data = {
            "category": bench["category"],
            "elf": bench["elf"],
            "description": bench["description"],
            "host_mips": round(res["mips"], 2),
            "host_ns_per_insn": round(res["ns_per_insn"], 2),
            "guest_cpi": round(res["guest_cpi"], 2),
            "retired_instructions": res["retired"],
            "category_mix": res["category_mix"],
        }
        results.append(bench_data)

        if not args.json:
            print(
                f" {bench['category']:<24} | {bench_data['host_mips']:>9.2f} MIPS | "
                f"{bench_data['host_ns_per_insn']:>10.2f} ns/insn | "
                f"{bench_data['guest_cpi']:>9.2f} | {bench['description']}"
            )

    if not args.json:
        print(f"-----------------------------------------------------------------------------------------\n")

    if args.json:
        print(json.dumps({"mode": args.mode, "steps": args.steps, "benchmarks": results}, indent=2))
    elif args.markdown:
        print("\n### Instruction Performance Summary Table (Markdown)\n")
        print("| Instruction Category | Host Throughput (MIPS) | Host Latency (ns/insn) | Guest CPI | Representative Instructions |")
        print("|:---------------------|-----------------------:|-----------------------:|----------:|:----------------------------|")
        for r in results:
            print(f"| {r['category']} | {r['host_mips']:.2f} MIPS | {r['host_ns_per_insn']:.2f} ns | {r['guest_cpi']:.2f} | {r['description']} |")


if __name__ == "__main__":
    main()

