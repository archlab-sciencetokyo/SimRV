#!/usr/bin/env python3
import sys
import os
import subprocess
import time
import statistics
import argparse
import json
import re
from shutil import which

def is_executable(path):
    return os.path.isfile(path) and os.access(path, os.X_OK)

def get_riscv_prefix():
    prefix = os.environ.get("RISCV_PREFIX")
    if prefix:
        return prefix
    if which("riscv64-unknown-elf-gcc"):
        return "riscv64-unknown-elf-"
    var_path = "/var/archlab-modules/riscv/2026.03.13/bin/riscv64-unknown-elf-"
    if os.path.exists(var_path + "gcc"):
        return var_path
    return ""

def get_tool_path(tool_name, env_var, prefix):
    path = os.environ.get(env_var)
    if path:
        return path
    if prefix:
        full_name = f"{prefix}{tool_name}"
        # If it's a path or just executable name
        if os.path.isabs(full_name) and is_executable(full_name):
            return full_name
        elif which(full_name):
            return which(full_name)
    
    for name in [f"riscv64-unknown-elf-{tool_name}", f"riscv32-unknown-elf-{tool_name}", tool_name]:
        w = which(name)
        if w:
            return w
    return None

def resolve_tohost(elf_path, nm_tool):
    if not nm_tool or not os.path.exists(elf_path):
        return None
    try:
        res = subprocess.run([nm_tool, "-g", elf_path], capture_output=True, text=True, timeout=5)
        for line in res.stdout.splitlines():
            parts = line.split()
            if len(parts) >= 3 and parts[2] == "tohost":
                # Ensure address prefix matches 0x
                addr = parts[0].lstrip('0')
                return f"0x{addr}" if addr else "0x0"
    except Exception:
        pass
    return None

def detect_xlen(simrv_bin):
    try:
        result = subprocess.run([simrv_bin, "--version"], capture_output=True, text=True, timeout=5)
        if "RV64" in result.stdout:
            return 64
        elif "RV32" in result.stdout:
            return 32
    except Exception:
        pass
    # Fallback to checking filename/path
    if "rv64" in simrv_bin.lower():
        return 64
    return 32

def parse_simrv_output(output):
    instrs = None
    kips = None
    
    for line in output.splitlines():
        if "Executed instructions" in line:
            parts = line.split(":")
            if len(parts) > 1:
                match = re.search(r'\(([\d,]+)\)', parts[1])
                if match:
                    instrs = int(match.group(1).replace(",", ""))
                else:
                    match = re.search(r'(\d+)', parts[1])
                    if match:
                        instrs = int(match.group(1))
        
        if "Simulation speed" in line:
            parts = line.split(":")
            if len(parts) > 1:
                match = re.search(r'([\d.,]+)\s+(MIPS|KIPS)', parts[1], re.IGNORECASE)
                if match:
                    val = float(match.group(1).replace(",", ""))
                    unit = match.group(2).upper()
                    if unit == "MIPS":
                        kips = val * 1000.0
                    else:
                        kips = val
    return instrs, kips

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
        "stddev": stddev
    }

def print_stats_table(simrv_stats, spike_stats, test_name, runs, insts):
    # Bold / color ANSI sequences if terminal supports it
    use_color = sys.stdout.isatty()
    BOLD = "\033[1m" if use_color else ""
    RESET = "\033[0m" if use_color else ""
    GREEN = "\033[32m" if use_color else ""
    CYAN = "\033[36m" if use_color else ""
    YELLOW = "\033[33m" if use_color else ""

    print(f"\n{BOLD}Benchmark Comparison Report{RESET}")
    print(f"==================================================")
    print(f"Test Name    : {test_name}")
    print(f"Runs         : {runs}")
    print(f"Instructions : {insts if insts else 'N/A'}")
    print(f"==================================================")
    
    # Header
    print(f"{BOLD}{'Metric / Statistic':<20} | {'SimRV':^12} | {'Spike':^12}{RESET}")
    print(f"---------------------+--------------+--------------")
    
    # Real Time
    print(f"{BOLD}{'Real Time (seconds)':<20}{RESET} |              | ")
    print(f"  Mean               | {simrv_stats['time']['mean']:>10.4f}   | {spike_stats['time']['mean']:>10.4f}")
    print(f"  Median             | {simrv_stats['time']['median']:>10.4f}   | {spike_stats['time']['median']:>10.4f}")
    print(f"  Min / Max          | {simrv_stats['time']['min']:>5.2f}/{simrv_stats['time']['max']:<4.2f}  | {spike_stats['time']['min']:>5.2f}/{spike_stats['time']['max']:<4.2f}")
    print(f"  Std Dev            | {simrv_stats['time']['stddev']:>10.4f}   | {spike_stats['time']['stddev']:>10.4f}")
    print(f"---------------------+--------------+--------------")
    
    # Wall-clock Speed KIPS
    print(f"{BOLD}{'Wall Speed (KIPS)':<20}{RESET} |              | ")
    print(f"  Mean               | {simrv_stats['wall_speed']['mean']:>10.2f}   | {spike_stats['wall_speed']['mean']:>10.2f}")
    print(f"  Median             | {simrv_stats['wall_speed']['median']:>10.2f}   | {spike_stats['wall_speed']['median']:>10.2f}")
    print(f"  Min / Max          | {simrv_stats['wall_speed']['min']:>5.0f}/{simrv_stats['wall_speed']['max']:<4.0f}  | {spike_stats['wall_speed']['min']:>5.0f}/{spike_stats['wall_speed']['max']:<4.0f}")
    print(f"  Std Dev            | {simrv_stats['wall_speed']['stddev']:>10.2f}   | {spike_stats['wall_speed']['stddev']:>10.2f}")
    print(f"---------------------+--------------+--------------")

    # Simulation Speed KIPS
    print(f"{BOLD}{'Sim Speed (KIPS)':<20}{RESET} |              | ")
    print(f"  Mean               | {simrv_stats['sim_speed']['mean']:>10.2f}   | {'N/A':^12}")
    print(f"  Median             | {simrv_stats['sim_speed']['median']:>10.2f}   | {'N/A':^12}")
    print(f"  Min / Max          | {simrv_stats['sim_speed']['min']:>5.0f}/{simrv_stats['sim_speed']['max']:<4.0f}  | {'N/A':^12}")
    print(f"  Std Dev            | {simrv_stats['sim_speed']['stddev']:>10.2f}   | {'N/A':^12}")
    print(f"==================================================")
    
    # Speedup ratio
    if simrv_stats['time']['mean'] > 0 and spike_stats['time']['mean'] > 0:
        speedup = spike_stats['time']['mean'] / simrv_stats['time']['mean']
        
        if speedup >= 1.0:
            print(f"{GREEN}SimRV is {speedup:.2f}x FASTER than Spike (wall-clock time){RESET}")
        else:
            print(f"{YELLOW}Spike is {1.0/speedup:.2f}x FASTER than SimRV (wall-clock time){RESET}")
            
        sim_vs_spike = simrv_stats['sim_speed']['mean'] / spike_stats['wall_speed']['mean'] if spike_stats['wall_speed']['mean'] > 0 else 0
        print(f"SimRV Core Engine is {sim_vs_spike:.2f}x Spike's speed (excluding startup overhead)")
    print(f"==================================================")

def main():
    parser = argparse.ArgumentParser(description="SimRV & Spike Benchmark Comparison Utility")
    parser.add_argument("--simrv", help="Path to SimRV binary")
    parser.add_argument("--spike", default="spike", help="Path to Spike binary")
    parser.add_argument("-n", "--runs", type=int, default=5, help="Number of iterations")
    parser.add_argument("-t", "--test", default="rv32ui-p-add", help="Test name or ELF/binary path")
    parser.add_argument("-H", "--tohost", help="Custom tohost MMIO address")
    parser.add_argument("-e", "--limit", type=int, default=2000000, help="Instruction limit")
    parser.add_argument("--timeout", type=int, default=20, help="Run timeout in seconds")
    parser.add_argument("--riscv-tests-dir", help="Path to riscv-tests directory")
    parser.add_argument("--json", help="Path to write JSON benchmark report")
    parser.add_argument("--isa", help="Override Spike ISA string (e.g. rv32gc)")
    
    args = parser.parse_args()
    
    if args.runs <= 0:
        print("ERROR: --runs must be a positive integer", file=sys.stderr)
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
        
    # 2. Resolve ELF path
    elf_path = args.test
    if not os.path.isfile(elf_path):
        elf_path = os.path.join(riscv_tests_dir, "isa", args.test)
        if not os.path.isfile(elf_path):
            # Fallback to default workspace dir if we checked env var first
            if riscv_tests_dir != default_dir and os.path.isdir(default_dir):
                fallback_dir = default_dir
                if os.path.isdir(os.path.join(fallback_dir, "share", "riscv-tests")):
                    fallback_dir = os.path.join(fallback_dir, "share", "riscv-tests")
                fallback_elf = os.path.join(fallback_dir, "isa", args.test)
                if os.path.isfile(fallback_elf):
                    riscv_tests_dir = fallback_dir
                    elf_path = fallback_elf
            
            # Re-check if we resolved it via fallback
            if not os.path.isfile(elf_path):
                print(f"ERROR: Test file not found: {args.test} (checked locally and at {elf_path})", file=sys.stderr)
            print("Please specify a valid --test or set RISCV_TESTS_DIR", file=sys.stderr)
            sys.exit(2)
            
    # 3. Resolve tools (objcopy, nm)
    prefix = get_riscv_prefix()
    objcopy_tool = get_tool_path("objcopy", "RISCV_OBJCOPY", prefix)
    nm_tool = get_tool_path("nm", "RISCV_NM", prefix)
    
    if not objcopy_tool:
        print("ERROR: objcopy tool not found. Set RISCV_OBJCOPY or RISCV_PREFIX.", file=sys.stderr)
        sys.exit(2)
        
    # 4. Generate .bin image for SimRV
    work_dir = os.path.join(root_dir, ".bench_tmp")
    log_dir = os.path.join(root_dir, "benchmark_logs")
    os.makedirs(work_dir, exist_ok=True)
    os.makedirs(log_dir, exist_ok=True)
    
    test_basename = os.path.basename(elf_path)
    bin_path = os.path.join(work_dir, f"{test_basename}.bin")
    
    # Objcopy conversion
    try:
        subprocess.run([objcopy_tool, "-O", "binary", elf_path, bin_path], check=True)
    except Exception as e:
        print(f"ERROR: objcopy conversion failed: {e}", file=sys.stderr)
        sys.exit(1)
        
    # 5. Resolve tohost address
    tohost_addr = args.tohost
    if not tohost_addr:
        tohost_addr = resolve_tohost(elf_path, nm_tool) or "0x80001000"
        
    # 6. Resolve SimRV binary
    simrv_bin = args.simrv
    if not simrv_bin:
        # Check standard locations
        simrv_32 = os.path.join(root_dir, "build/rv32-release/SimRV")
        simrv_64 = os.path.join(root_dir, "build/rv64-release/SimRV")
        if is_executable(simrv_32):
            simrv_bin = simrv_32
        elif is_executable(simrv_64):
            simrv_bin = simrv_64
        else:
            simrv_bin = os.path.join(root_dir, "SimRV")
            
    if not is_executable(simrv_bin):
        print(f"ERROR: SimRV binary not executable: {simrv_bin}", file=sys.stderr)
        sys.exit(1)
        
    # 7. Detect XLEN & Spike ISA
    xlen = detect_xlen(simrv_bin)
    isa = args.isa or (f"rv{xlen}gc" if xlen else "rv32gc")
    
    print("== SimRV vs Spike Benchmark ==")
    print(f"SimRV binary : {simrv_bin} (RV{xlen})")
    print(f"Spike binary : {args.spike}")
    print(f"ELF path     : {elf_path}")
    print(f"Binary path  : {bin_path}")
    print(f"Tohost addr  : {tohost_addr}")
    print(f"Spike ISA    : {isa}")
    print(f"Runs         : {args.runs} | Limit: {args.limit} | Timeout: {args.timeout}s")
    
    simrv_times = []
    simrv_speeds = []
    simrv_instrs = None
    
    # --- Run SimRV ---
    print(f"\nRunning SimRV...")
    for i in range(1, args.runs + 1):
        print(f"  [RUN] SimRV iter {i}/{args.runs}")
        log_file = os.path.join(log_dir, f"bench_simrv_{i}.log")
        
        simrv_cmd = [
            simrv_bin,
            "-m", bin_path,
            "-e", str(args.limit),
            "-T",
            "-H", tohost_addr
        ]
        
        start_t = time.perf_counter()
        try:
            res = subprocess.run(simrv_cmd, capture_output=True, text=True, timeout=args.timeout, stdin=subprocess.DEVNULL)
            end_t = time.perf_counter()
            
            with open(log_file, "w") as lf:
                lf.write(res.stdout)
                lf.write(res.stderr)
                
            if res.returncode != 0:
                print(f"ERROR: SimRV failed with exit code {res.returncode}. See {log_file}", file=sys.stderr)
                sys.exit(1)
                
            insts, kips = parse_simrv_output(res.stdout + "\n" + res.stderr)
            if kips is None:
                print(f"ERROR: Could not parse simulation speed from SimRV output. See {log_file}", file=sys.stderr)
                sys.exit(1)
                
            elapsed = end_t - start_t
            simrv_times.append(elapsed)
            simrv_speeds.append(kips)
            if insts:
                simrv_instrs = insts
                
        except subprocess.TimeoutExpired:
            print(f"ERROR: SimRV timed out (iter {i})", file=sys.stderr)
            sys.exit(1)
            
    # --- Run Spike ---
    spike_times = []
    spike_speeds = []
    
    # Check if spike is in PATH
    if not which(args.spike) and not os.path.exists(args.spike):
        print(f"\nWARNING: Spike binary '{args.spike}' not found. Skipping Spike benchmark.")
        spike_stats = {
            "time": calculate_stats([]),
            "wall_speed": calculate_stats([])
        }
        simrv_wall_speeds = []
        if simrv_instrs:
            simrv_wall_speeds = [(simrv_instrs / t) / 1000.0 for t in simrv_times]
        simrv_stats = {
            "time": calculate_stats(simrv_times),
            "sim_speed": calculate_stats(simrv_speeds),
            "wall_speed": calculate_stats(simrv_wall_speeds)
        }
    else:
        print(f"\nRunning Spike...")
        for i in range(1, args.runs + 1):
            print(f"  [RUN] Spike iter {i}/{args.runs}")
            
            spike_cmd = [
                args.spike,
                f"--isa={isa}",
                elf_path
            ]
            
            start_t = time.perf_counter()
            try:
                # Spike redirects console output to stdout, which we can ignore
                res = subprocess.run(spike_cmd, capture_output=True, timeout=args.timeout, stdin=subprocess.DEVNULL)
                end_t = time.perf_counter()
                
                if res.returncode != 0:
                    pass
                    
                elapsed = end_t - start_t
                spike_times.append(elapsed)
                
                # Calculate Spike's speed in KIPS using SimRV instruction count
                if simrv_instrs:
                    spike_kips = (simrv_instrs / elapsed) / 1000.0
                    spike_speeds.append(spike_kips)
                else:
                    spike_speeds.append(0.0)
                    
            except subprocess.TimeoutExpired:
                print(f"ERROR: Spike timed out (iter {i})", file=sys.stderr)
                sys.exit(1)
                
        simrv_wall_speeds = []
        if simrv_instrs:
            simrv_wall_speeds = [(simrv_instrs / t) / 1000.0 for t in simrv_times]
        else:
            simrv_wall_speeds = [0.0] * len(simrv_times)

        simrv_stats = {
            "time": calculate_stats(simrv_times),
            "sim_speed": calculate_stats(simrv_speeds),
            "wall_speed": calculate_stats(simrv_wall_speeds)
        }
        spike_stats = {
            "time": calculate_stats(spike_times),
            "wall_speed": calculate_stats(spike_speeds)
        }
        
    # Print comparison
    print_stats_table(simrv_stats, spike_stats, test_basename, args.runs, simrv_instrs)
    
    # 8. Output to JSON if requested
    if args.json:
        report = {
            "test_name": test_basename,
            "runs": args.runs,
            "instructions": simrv_instrs,
            "xlen": xlen,
            "spike_isa": isa,
            "simrv": {
                "runs_time": simrv_times,
                "runs_sim_speed_kips": simrv_speeds,
                "runs_wall_speed_kips": simrv_wall_speeds,
                "stats": simrv_stats
            },
            "spike": {
                "runs_time": spike_times,
                "runs_wall_speed_kips": spike_speeds,
                "stats": spike_stats
            }
        }
        try:
            with open(args.json, "w") as jf:
                json.dump(report, jf, indent=2)
            print(f"Detailed JSON report written to: {args.json}")
        except Exception as e:
            print(f"ERROR: Failed to write JSON report: {e}", file=sys.stderr)

if __name__ == "__main__":
    main()
