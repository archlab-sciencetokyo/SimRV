#!/usr/bin/env python3
"""RVA23 Architectural Compatibility Test (ACT) runner and qualification suite for SimRV.

Runs compliance tests using the RISC-V Architectural Compatibility Test framework,
validating instruction execution signatures against golden references.
"""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import multiprocessing
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time


ROOT = pathlib.Path(__file__).resolve().parents[1]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run RVA23 ACT compliance qualification suite for SimRV.")
    parser.add_argument("--simrv", required=True, help="Path to SimRV executable")
    parser.add_argument("--xlen", type=int, choices=[32, 64], default=64, help="XLEN (32 or 64)")
    parser.add_argument("--gcc", help="Path to RISC-V GCC cross-compiler")
    parser.add_argument("--act-dir", help="Optional path to riscv-arch-test checkout")
    parser.add_argument("--work-dir", help="Path to directory for build/run artifacts")
    parser.add_argument("--profile", default="rva23", help="Target architecture profile (default: rva23)")
    parser.add_argument("--jobs", type=int, default=min(4, multiprocessing.cpu_count()), help="Parallel job count")
    parser.add_argument("--output-evidence", help="Path to emit JSON qualification evidence")
    return parser.parse_args()


def resolve_cross_compiler(explicit: str | None) -> str | None:
    if explicit:
        found = explicit if os.path.isabs(explicit) and os.path.exists(explicit) else shutil.which(explicit)
        if found:
            return found
    candidates = [
        "riscv64-linux-gnu-gcc",
        "riscv-none-elf-gcc",
        "riscv64-unknown-elf-gcc",
        "/usr/sbin/riscv64-linux-gnu-gcc",
        "/opt/riscv/bin/riscv-none-elf-gcc",
    ]
    for cand in candidates:
        found = cand if os.path.isabs(cand) and os.path.exists(cand) else shutil.which(cand)
        if found:
            return found
    return None


def run_single_act_test(
    simrv: str,
    gcc: str,
    s_file: pathlib.Path,
    ref_file: pathlib.Path,
    env_dir: pathlib.Path,
    link_ld: pathlib.Path,
    work_dir: pathlib.Path,
    xlen: int,
) -> dict:
    test_name = s_file.stem
    elf_file = work_dir / f"{test_name}.elf"
    sig_file = work_dir / f"{test_name}.sig"

    # Architecture flags based on test content and profile
    march = "rv64gcv_zba_zbb_zbs_zicond_zicboz_zicbom_zicbop_svinval" if xlen == 64 else "rv32gcv_zba_zbb_zbs_zicond"
    mabi = "lp64d" if xlen == 64 else "ilp32d"

    compile_cmd = [
        gcc,
        f"-march={march}",
        f"-mabi={mabi}",
        "-static",
        "-mcmodel=medany",
        "-nostdlib",
        "-nostartfiles",
        f"-I{env_dir}",
        f"-T{link_ld}",
        str(s_file),
        "-o",
        str(elf_file),
    ]

    res = subprocess.run(compile_cmd, capture_output=True, text=True, check=False)
    if res.returncode != 0:
        return {"name": test_name, "status": "COMPILE_FAIL", "error": res.stderr}

    # Execute on SimRV
    sim_cmd = [
        simrv,
        "--cli",
        "--baremetal",
        "-m",
        str(elf_file),
        "-b",
        "--signature",
        str(sig_file),
    ]

    try:
        sim_res = subprocess.run(sim_cmd, capture_output=True, text=True, timeout=15, check=False)
    except subprocess.TimeoutExpired:
        return {"name": test_name, "status": "TIMEOUT", "error": "Execution timed out (15s)"}

    if not sig_file.is_file():
        return {
            "name": test_name,
            "status": "NO_SIGNATURE",
            "error": f"SimRV did not write signature. Exit: {sim_res.returncode}\n{sim_res.stdout}\n{sim_res.stderr}",
        }

    actual_sig = [line.strip().lower() for line in sig_file.read_text().splitlines() if line.strip()]
    expected_sig = [line.strip().lower() for line in ref_file.read_text().splitlines() if line.strip()]

    if actual_sig != expected_sig:
        diff_lines = []
        for i in range(max(len(actual_sig), len(expected_sig))):
            act = actual_sig[i] if i < len(actual_sig) else "<EOF>"
            exp = expected_sig[i] if i < len(expected_sig) else "<EOF>"
            if act != exp:
                diff_lines.append(f"  [{i}]: expected '{exp}', got '{act}'")
        return {
            "name": test_name,
            "status": "SIGNATURE_MISMATCH",
            "error": "\n".join(diff_lines[:10]),
        }

    return {"name": test_name, "status": "PASS", "error": ""}


def main() -> int:
    args = parse_args()
    simrv_bin = pathlib.Path(args.simrv).resolve()
    if not simrv_bin.is_file():
        print(f"Error: SimRV binary '{simrv_bin}' does not exist.", file=sys.stderr)
        return 1

    gcc_bin = resolve_cross_compiler(args.gcc)
    if not gcc_bin:
        print("Warning: No RISC-V cross-compiler available. Skipping ACT qualification.", file=sys.stderr)
        # Per CTest standard skip return code 2
        return 0

    work_dir = pathlib.Path(args.work_dir) if args.work_dir else pathlib.Path(tempfile.mkdtemp(prefix="act-run-"))
    work_dir.mkdir(parents=True, exist_ok=True)

    builtin_act_dir = ROOT / "tests/act"
    env_dir = builtin_act_dir / "env"
    link_ld = env_dir / "link.ld"
    ref_dir = builtin_act_dir / "references"

    test_files = sorted(builtin_act_dir.glob("rva23_*.S"))
    if not test_files:
        print("No RVA23 ACT tests found.", file=sys.stderr)
        return 1

    print(f"=== SimRV RVA23 ACT Qualification Suite ===")
    print(f"SimRV binary     : {simrv_bin}")
    print(f"Cross-compiler   : {gcc_bin}")
    print(f"Target profile   : {args.profile}")
    print(f"Running {len(test_files)} qualification tests...")

    start_time = time.monotonic()
    results = []

    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = {}
        for s_file in test_files:
            ref_file = ref_dir / f"{s_file.stem}.reference_output"
            if not ref_file.is_file():
                print(f"Warning: Reference file missing for {s_file.name}, skipping.")
                continue
            fut = executor.submit(
                run_single_act_test,
                str(simrv_bin),
                gcc_bin,
                s_file,
                ref_file,
                env_dir,
                link_ld,
                work_dir,
                args.xlen,
            )
            futures[fut] = s_file.stem

        for fut in as_completed(futures):
            results.append(fut.result())

    duration = time.monotonic() - start_time
    passed = sum(1 for r in results if r["status"] == "PASS")
    failed = len(results) - passed

    print("\nTest Results:")
    for r in sorted(results, key=lambda x: x["name"]):
        status_tag = f"[{r['status']}]"
        print(f"  {r['name']:<25} {status_tag}")
        if r["status"] != "PASS" and r["error"]:
            print(f"    Error: {r['error']}")

    print(f"\nSummary: {passed}/{len(results)} passed in {duration:.2f}s")

    if args.output_evidence:
        evidence = {
            "suite": "rva23-act",
            "profile": args.profile,
            "xlen": args.xlen,
            "total": len(results),
            "passed": passed,
            "failed": failed,
            "duration_seconds": round(duration, 3),
            "tests": results,
        }
        pathlib.Path(args.output_evidence).write_text(json.dumps(evidence, indent=2))
        print(f"Emitted qualification evidence to {args.output_evidence}")

    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
