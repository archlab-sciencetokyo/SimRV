#!/usr/bin/env python3
"""Preflight and run the complete SimRV paper artifact on a benchmark server."""

import argparse
import json
import os
import pathlib
import platform
import shutil
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]


def executable(name: str) -> str | None:
    value = shutil.which(name)
    return str(pathlib.Path(value).resolve()) if value else None


def first_executable(*names: str) -> str | None:
    return next((path for name in names if (path := executable(name))), None)


def default_spike() -> str:
    installed = executable("spike")
    local = ROOT / ".cache/repro/spike/build/spike"
    return installed or str(local)


def preflight(args: argparse.Namespace) -> tuple[dict, list[str]]:
    tools = {name: executable(name) for name in
             ("cmake", "ninja", "git", "gcc", "g++", "clang", "clang++", "go")}
    tools["riscv-objcopy"] = first_executable(
        "riscv64-unknown-elf-objcopy", "riscv64-linux-gnu-objcopy",
        "riscv32-unknown-elf-objcopy", "riscv32-linux-gnu-objcopy")
    tools["riscv-nm"] = first_executable(
        "riscv64-unknown-elf-nm", "riscv64-linux-gnu-nm",
        "riscv32-unknown-elf-nm", "riscv32-linux-gnu-nm")
    paths = {
        "riscv_tests": str(args.riscv_tests_dir.resolve()),
        "vector_tests": str(args.vector_tests_dir.resolve()),
        "spike": str(pathlib.Path(args.spike).resolve()) if "/" in args.spike else executable(args.spike),
        "linux_rv32": str((args.linux_images_root / "rv32").resolve()),
        "linux_rv64": str((args.linux_images_root / "rv64").resolve()),
    }
    missing = [f"tool:{name}" for name, path in tools.items() if not path]
    missing.extend(f"path:{name}" for name, path in paths.items()
                   if not path or not pathlib.Path(path).exists())
    report = {"schema_version": 1, "host": platform.platform(),
              "python": sys.version.split()[0], "tools": tools, "paths": paths,
              "environment": {key: os.environ.get(key) for key in
                              ("CC", "CXX", "RISCV_PREFIX")}, "ready": not missing,
              "missing": missing}
    return report, missing


def reproduction_command(args: argparse.Namespace) -> list[str]:
    command = [sys.executable, "scripts/reproduce.py", "--mode", "full",
               "--output", str(args.output.resolve()),
               "--riscv-tests-dir", str(args.riscv_tests_dir.resolve()),
               "--vector-tests-dir", str(args.vector_tests_dir.resolve()),
               "--spike", args.spike,
               "--linux-images-root", str(args.linux_images_root.resolve()),
               "--archive", str(args.archive.resolve())]
    if args.baseline:
        command.extend(["--baseline", str(args.baseline.resolve())])
    return command


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--riscv-tests-dir", type=pathlib.Path,
                        default=ROOT / ".cache/repro/riscv-tests")
    parser.add_argument("--vector-tests-dir", type=pathlib.Path,
                        default=ROOT / ".cache/repro/vector-tests")
    parser.add_argument("--linux-images-root", type=pathlib.Path,
                        default=ROOT / "linux-images")
    parser.add_argument("--spike", default=default_spike())
    parser.add_argument("--output", type=pathlib.Path, default=ROOT / "repro/results")
    parser.add_argument("--archive", type=pathlib.Path,
                        default=ROOT / "SimRV-paper-artifact.tar.gz")
    parser.add_argument("--baseline", type=pathlib.Path)
    parser.add_argument("--preflight-only", action="store_true")
    args = parser.parse_args()

    report, missing = preflight(args)
    print(json.dumps(report, indent=2, sort_keys=True))
    if missing:
        raise SystemExit("server preflight failed: " + ", ".join(missing))
    if args.preflight_only:
        return
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "server-preflight.json").write_text(
        json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    command = reproduction_command(args)
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
