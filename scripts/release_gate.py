#!/usr/bin/env python3
"""Run the reproducible local SimRV 3.0 release validation matrix."""

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import time


ROOT = pathlib.Path(__file__).resolve().parents[1]


def run(command: list[str], env: dict[str, str] | None = None) -> float:
    print("+", " ".join(command), flush=True)
    start = time.monotonic()
    subprocess.run(command, cwd=ROOT, env=env, check=True)
    return time.monotonic() - start


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--iterations", type=int, default=2)
    parser.add_argument("--compiler", choices=["gcc", "clang", "all"], default="all")
    parser.add_argument("--arch", choices=["rv32", "rv64", "all"], default="all")
    parser.add_argument("--output", type=pathlib.Path, default=ROOT / "release-report.json")
    args = parser.parse_args()
    if args.iterations < 1:
        parser.error("--iterations must be positive")

    compilers = ["gcc", "clang"] if args.compiler == "all" else [args.compiler]
    arches = ["rv32", "rv64"] if args.arch == "all" else [args.arch]
    compiler_bins = {"gcc": ("gcc", "g++"), "clang": ("clang", "clang++")}
    report = {"iterations": args.iterations, "configurations": []}

    run([sys.executable, "scripts/release_check.py"])
    for iteration in range(1, args.iterations + 1):
        for compiler in compilers:
            cc, cxx = compiler_bins[compiler]
            if not shutil.which(cxx):
                raise SystemExit(f"required compiler not found: {cxx}")
            for arch in arches:
                xlen = arch.removeprefix("rv")
                build_dir = ROOT / "build" / "release-gate" / f"run{iteration}-{compiler}-{arch}"
                if build_dir.exists():
                    shutil.rmtree(build_dir)
                configure = [
                    "cmake", "-S", ".", "-B", str(build_dir), "-G", "Ninja",
                    "-DCMAKE_BUILD_TYPE=Release", f"-DSIMRV_XLEN={xlen}",
                    f"-DCMAKE_C_COMPILER={cc}", f"-DCMAKE_CXX_COMPILER={cxx}",
                    "-DSIMRV_WARNINGS_AS_ERRORS=ON",
                ]
                elapsed = run(configure)
                elapsed += run(["cmake", "--build", str(build_dir)])
                elapsed += run([
                    "ctest", "--test-dir", str(build_dir), "--output-on-failure", "-L", "gate"
                ], env=os.environ.copy())
                binary = build_dir / "SimRV"
                run([sys.executable, "scripts/release_check.py", "--binary", str(binary)])
                report["configurations"].append({
                    "iteration": iteration,
                    "compiler": compiler,
                    "architecture": arch,
                    "elapsed_seconds": elapsed,
                    "binary_size_bytes": binary.stat().st_size,
                })

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"release gate passed; report: {args.output}")


if __name__ == "__main__":
    main()
