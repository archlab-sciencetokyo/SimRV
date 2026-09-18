#!/usr/bin/env python3
"""
SimRV CPU Model Calibration & Auto-Tuning Tool

Calibrates and auto-tunes microarchitecture timing parameters (.cfg) against
hardware RTL / Verilator cycle counts to minimize cycle disparity.
"""

import argparse
import configparser
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

def extract_number(pattern, text):
    m = re.search(pattern, text)
    return int(m.group(1).replace(',', '')) if m else None

def run_simrv(simrv_bin: Path, cfg_path: Path, elf_path: Path, repo_root: Path) -> dict:
    cmd = [
        str(simrv_bin),
        "--cli",
        "--ca",
        "-m", str(elf_path.resolve()),
        "-H", "0x80000000",
        "--cpu-config", str(cfg_path.resolve())
    ]
    res = subprocess.run(cmd, cwd=repo_root, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if res.returncode != 0:
        return {"error": f"SimRV exited with {res.returncode}", "output": res.stdout}

    cycles = extract_number(r'Elapsed cycles \(clocks\)\s*:\s*[\d\.]*K?\s*\(([0-9,]+)\)', res.stdout)
    insts = extract_number(r'Executed instructions\s*:\s*[\d\.]*K?\s*\(([0-9,]+)\)', res.stdout)
    raw_stalls = extract_number(r'-\s*Data RAW Stalls\s*:\s*([0-9,]+)', res.stdout)
    ctrl_bubbles = extract_number(r'-\s*Control Bubbles\s*:\s*(\d+)', res.stdout)
    ic_miss = extract_number(r'-\s*ICache Miss Stalls\s*:\s*([0-9,]+)', res.stdout)
    dc_miss = extract_number(r'-\s*DCache Miss Stalls\s*:\s*([0-9,]+)', res.stdout)

    return {
        "cycles": cycles,
        "insts": insts,
        "raw_stalls": raw_stalls,
        "ctrl_bubbles": ctrl_bubbles,
        "ic_miss_stalls": ic_miss,
        "dc_miss_stalls": dc_miss,
    }

def evaluate_config(simrv_bin: Path, cfg_path: Path, benchmarks_dir: Path, rtl_targets: dict, repo_root: Path) -> dict:
    results = {}
    total_abs_err_pct = 0.0
    count = 0

    for name, target in rtl_targets.items():
        elf_path = benchmarks_dir / f"{name}.elf"
        if not elf_path.exists():
            continue
        sim = run_simrv(simrv_bin, cfg_path, elf_path, repo_root)
        if "error" in sim or sim.get("cycles") is None:
            return {"error": sim.get("error", "Failed to get cycles"), "mape": float("inf")}

        sim_cycles = sim["cycles"]
        rtl_cycles = target.get("core_estimate_cycle") or target.get("mcycle")
        delta = abs(sim_cycles - rtl_cycles)
        err_pct = (delta / rtl_cycles) * 100.0 if rtl_cycles else 0.0

        results[name] = {
            "sim_cycles": sim_cycles,
            "rtl_cycles": rtl_cycles,
            "delta": delta,
            "err_pct": round(err_pct, 2)
        }
        total_abs_err_pct += err_pct
        count += 1

    mape = total_abs_err_pct / count if count > 0 else float("inf")
    return {"results": results, "mape": round(mape, 2)}

def main():
    parser = argparse.ArgumentParser(description="Auto-tune SimRV CPU model configuration against RTL benchmarks")
    parser.add_argument("--base-config", "-c", default="configs/models/rvcomp.cfg", help="Base .cfg file to calibrate")
    parser.add_argument("--simrv-bin", default=None, help="Path to SimRV executable")
    parser.add_argument("--eval-json", default="build/rvcomp_parity.json", help="Path to RTL parity evaluation JSON")
    parser.add_argument("--apply", action="store_true", help="Apply the best calibrated parameters to the base config")
    parser.add_argument("--quick", action="store_true", help="Perform a quick focused calibration sweep")
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[1]
    base_cfg_path = (repo_root / args.base_config).resolve() if not Path(args.base_config).is_absolute() else Path(args.base_config)
    if not base_cfg_path.exists():
        print(f"Error: Base config not found: {base_cfg_path}", file=sys.stderr)
        sys.exit(1)

    eval_json_path = (repo_root / args.eval_json).resolve() if not Path(args.eval_json).is_absolute() else Path(args.eval_json)
    if not eval_json_path.exists():
        print(f"[*] RTL parity JSON not found at {eval_json_path}. Running scripts/evaluate_rvcomp.py first...")
        subprocess.run([sys.executable, str(repo_root / "scripts/evaluate_rvcomp.py")], cwd=repo_root, check=True)

    with open(eval_json_path, "r", encoding="utf-8") as f:
        parity_data = json.load(f)

    rtl_targets = {}
    for name, item in parity_data.items():
        rtl_data = item.get("rtl_verilator", {})
        rtl_targets[name] = {
            "mcycle": rtl_data.get("mcycle"),
            "core_estimate_cycle": rtl_data.get("core_estimate_cycle", rtl_data.get("mcycle"))
        }

    benchmarks_dir = repo_root / "build/rvcomp_eval/build"
    if not benchmarks_dir.exists():
        print(f"Error: Benchmark ELFs not found under {benchmarks_dir}", file=sys.stderr)
        sys.exit(1)

    if args.simrv_bin:
        simrv_bin = Path(args.simrv_bin).resolve()
    else:
        simrv_bin = repo_root / "build/rv64-release/SimRV"
        if not simrv_bin.exists():
            simrv_bin = repo_root / "build/rv32-release/SimRV"
    if not simrv_bin.exists():
        print(f"Error: SimRV binary not found at {simrv_bin}", file=sys.stderr)
        sys.exit(1)

    print(f"\033[1;34m=== SimRV CPU Model Calibration Wizard ===\033[0m")
    print(f"  Base Model Config : {base_cfg_path}")
    print(f"  SimRV Executable  : {simrv_bin}")
    print(f"  Target Benchmarks : {list(rtl_targets.keys())}")

    # 1. Baseline Evaluation
    baseline_eval = evaluate_config(simrv_bin, base_cfg_path, benchmarks_dir, rtl_targets, repo_root)
    print(f"\n\033[1;33m[*] Baseline Evaluation (current parameters):\033[0m")
    for name, data in baseline_eval.get("results", {}).items():
        print(f"  - {name:<20}: SimRV={data['sim_cycles']:<5} | RTL={data['rtl_cycles']:<5} | Delta={data['delta']:<4} ({data['err_pct']}%)")
    print(f"  \033[1mBaseline MAPE\033[0m     : \033[1;31m{baseline_eval['mape']}%\033[0m")

    # 2. Parameter Sweep
    cp_orig = configparser.ConfigParser()
    cp_orig.read(base_cfg_path, encoding="utf-8")

    best_mape = baseline_eval["mape"]
    best_config = cp_orig
    best_eval = baseline_eval
    best_params = {}

    # Define sweep grid
    # For RVComp:
    # L0 ICache capacity in RTL is 1024 bytes, 16-byte block.
    # L0 miss to L1 hit takes 3-4 cycles, L0 miss to DRAM takes ~15-16 cycles.
    # LSU load hit latency is ~3 cycles, store latency is ~8-12 cycles.
    if args.quick:
        bus_req_latencies = [1, 4, 8, 16, 24, 30]
        bus_resp_latencies = [1, 4, 8, 16, 24, 30]
        dc_hit_latencies = [4, 6, 8, 10]
    else:
        bus_req_latencies = [1, 4, 8, 12, 16, 20, 24, 28, 32]
        bus_resp_latencies = [1, 4, 8, 12, 16, 20, 24, 28, 32]
        dc_hit_latencies = [4, 6, 7, 8, 9, 10]

    print(f"\n[*] Commencing systematic parameter search...")
    with tempfile.NamedTemporaryFile(mode="w", suffix=".cfg", delete=False) as tmp:
        tmp_cfg_path = Path(tmp.name)

    try:
        tested_count = 0
        for req_lat in bus_req_latencies:
            for resp_lat in bus_resp_latencies:
                for dc_hit in dc_hit_latencies:
                    cp = configparser.ConfigParser()
                    cp.read(base_cfg_path, encoding="utf-8")

                    if "instruction_cache" not in cp: cp["instruction_cache"] = {}
                    if "data_cache" not in cp: cp["data_cache"] = {}
                    if "interconnect" not in cp: cp["interconnect"] = {}

                    cp["instruction_cache"]["capacity_bytes"] = "1024"
                    cp["data_cache"]["hit_latency"] = str(dc_hit)
                    cp["interconnect"]["request_latency"] = str(req_lat)
                    cp["interconnect"]["response_latency"] = str(resp_lat)

                    with open(tmp_cfg_path, "w", encoding="utf-8") as f:
                        cp.write(f)

                    ev = evaluate_config(simrv_bin, tmp_cfg_path, benchmarks_dir, rtl_targets, repo_root)
                    tested_count += 1
                    if ev.get("mape", float("inf")) < best_mape:
                        best_mape = ev["mape"]
                        best_eval = ev
                        best_config = cp
                        best_params = {
                            "dcache_hit_latency": dc_hit,
                            "interconnect_req": req_lat,
                            "interconnect_resp": resp_lat
                        }
                        print(f"  -> [{tested_count}] New optimum MAPE: \033[1;32m{best_mape}%\033[0m with {best_params}")

    finally:
        if tmp_cfg_path.exists():
            tmp_cfg_path.unlink()

    print(f"\n\033[1;32m=== Tuning Results ===\033[0m")
    print(f"  Configurations evaluated: {tested_count}")
    print(f"  Baseline Error (MAPE)   : \033[1;31m{baseline_eval['mape']}%\033[0m")
    print(f"  Optimal Error  (MAPE)   : \033[1;32m{best_mape}%\033[0m")
    print(f"  Error Reduction         : {round(baseline_eval['mape'] - best_mape, 2)}% percentage points")
    print(f"  Optimal Parameters      : {best_params}")

    print(f"\n\033[1;34m[*] Detailed Parity Comparison (Optimal):\033[0m")
    for name, data in best_eval.get("results", {}).items():
        base_data = baseline_eval["results"].get(name, {})
        base_err = base_data.get("err_pct", "N/A")
        print(f"  - {name:<20}: SimRV={data['sim_cycles']:<5} | RTL={data['rtl_cycles']:<5} | Err: {data['err_pct']}% (was {base_err}%)")

    if args.apply:
        print(f"\n[*] Applying optimal parameters to {base_cfg_path}...")
        with open(base_cfg_path, "w", encoding="utf-8") as f:
            best_config.write(f)
        print("\033[1;32m[DONE]\033[0m Configuration updated successfully.")
    else:
        print(f"\n(To write these optimal parameters back to {base_cfg_path}, run with --apply)")

if __name__ == "__main__":
    main()
