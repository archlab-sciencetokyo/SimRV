#!/usr/bin/env python3
"""Run generated RISC-V ACT4 self-checking ELFs against SimRV."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import os
from pathlib import Path
import subprocess
import sys
import time


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--simrv", required=True, type=Path, help="SimRV executable")
    parser.add_argument("--elf-dir", required=True, type=Path, help="ACT4 generated elfs directory")
    parser.add_argument("--report", required=True, type=Path, help="JSON report output path")
    parser.add_argument("--jobs", type=int, default=min(12, os.cpu_count() or 1))
    parser.add_argument("--timeout", type=float, default=30.0, help="Per-test timeout in seconds")
    parser.add_argument("--max-failures", type=int, default=30, help="Maximum failure details printed")
    return parser.parse_args()


def run_test(simrv: Path, elf: Path, timeout: float) -> dict[str, object]:
    started = time.monotonic()
    try:
        result = subprocess.run(
            [str(simrv), "--cli", "--baremetal", "--no-sbi", "--vlen", "128", "-m", str(elf)],
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
            check=False,
        )
        output = result.stdout + result.stderr
        passed = (
            result.returncode == 0
            and (
                "ISA TEST PASS" in output
                or "Termination reason       :         guest poweroff" in output
            )
            and "ISA TEST FAIL" not in output
            and "RVCP-SUMMARY: TEST FAILED" not in output
            and "[FATAL]" not in output
        )
        status = "PASS" if passed else "FAIL"
        detail = "" if passed else output[-4000:]
        return {
            "name": elf.name,
            "path": str(elf),
            "status": status,
            "returncode": result.returncode,
            "duration_seconds": round(time.monotonic() - started, 3),
            "detail": detail,
        }
    except subprocess.TimeoutExpired as exc:
        return {
            "name": elf.name,
            "path": str(elf),
            "status": "TIMEOUT",
            "returncode": None,
            "duration_seconds": round(time.monotonic() - started, 3),
            "detail": f"Exceeded {timeout:g} seconds",
        }


def main() -> int:
    args = parse_args()
    simrv = args.simrv.resolve()
    elf_dir = args.elf_dir.resolve()
    if not simrv.is_file() or not os.access(simrv, os.X_OK):
        print(f"SimRV executable is not runnable: {simrv}", file=sys.stderr)
        return 2
    if not elf_dir.is_dir():
        print(f"ACT4 ELF directory does not exist: {elf_dir}", file=sys.stderr)
        return 2
    if args.jobs < 1:
        print("--jobs must be positive", file=sys.stderr)
        return 2

    elfs = sorted(elf_dir.rglob("*.elf"))
    if not elfs:
        print(f"No ACT4 ELFs found under: {elf_dir}", file=sys.stderr)
        return 2

    started = time.monotonic()
    results: list[dict[str, object]] = []
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = {executor.submit(run_test, simrv, elf, args.timeout): elf for elf in elfs}
        for future in as_completed(futures):
            results.append(future.result())

    results.sort(key=lambda item: str(item["path"]))
    passed = sum(item["status"] == "PASS" for item in results)
    failed = [item for item in results if item["status"] != "PASS"]
    report = {
        "suite": "RISC-V ACT4 RVA23 certification ELFs",
        "simrv": str(simrv),
        "elf_dir": str(elf_dir),
        "total": len(results),
        "passed": passed,
        "failed": len(failed),
        "jobs": args.jobs,
        "duration_seconds": round(time.monotonic() - started, 3),
        "results": results,
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n")

    print(f"ACT4: {passed}/{len(results)} passed; {len(failed)} failed")
    print(f"Duration: {report['duration_seconds']}s")
    print(f"Report: {args.report.resolve()}")
    counts: dict[str, int] = {}
    for item in failed:
        key = f"return code {item['returncode']}" if item["status"] == "FAIL" else str(item["status"])
        counts[key] = counts.get(key, 0) + 1
    for key, count in sorted(counts.items()):
        print(f"{key}: {count}")
    for item in failed[: args.max_failures]:
        print(f"[{item['status']}] {item['path']}")
        detail = str(item["detail"])
        if detail:
            print(detail)
    if len(failed) > args.max_failures:
        print(f"... {len(failed) - args.max_failures} additional failures are in the JSON report")
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
