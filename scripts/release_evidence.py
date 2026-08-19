#!/usr/bin/env python3
"""Run CTest suites and emit versioned, machine-readable release evidence."""

import argparse
import json
import os
import pathlib
import platform
import re
import subprocess
import sys
import time


ROOT = pathlib.Path(__file__).resolve().parents[1]
MANIFEST = json.loads((ROOT / "release/release-manifest.json").read_text())


def command_version(command: list[str]) -> str:
    try:
        result = subprocess.run(command, text=True, capture_output=True, timeout=10, check=False)
        return (result.stdout or result.stderr).splitlines()[0].strip()
    except (OSError, subprocess.SubprocessError, IndexError):
        return "unavailable"


def git_revision(path: pathlib.Path) -> str:
    try:
        return subprocess.run(
            ["git", "-C", str(path), "rev-parse", "HEAD"], text=True,
            capture_output=True, timeout=10, check=True,
        ).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return "unavailable"


def registered_count(build_dir: pathlib.Path, label: str) -> int:
    result = subprocess.run(
        ["ctest", "--test-dir", str(build_dir), "--show-only=json-v1", "-L", label],
        text=True, capture_output=True, check=False,
    )
    if result.returncode:
        return 0
    return len(json.loads(result.stdout).get("tests", []))


def run_suite(build_dir: pathlib.Path, suite: dict) -> dict:
    label = suite["ctest_label"]
    registered = registered_count(build_dir, label)
    started = time.monotonic()
    if registered == 0:
        return {"id": suite["id"], "label": label, "status": "unavailable", "registered": 0,
                "passed": 0, "failed": 0, "skipped": 0, "duration_seconds": 0.0,
                "reason": "no matching tests registered"}
    result = subprocess.run(
        ["ctest", "--test-dir", str(build_dir), "--output-on-failure", "-L", label],
        text=True, capture_output=True, check=False,
    )
    output = result.stdout + result.stderr
    skipped_match = re.search(r"(\d+) tests? skipped", output, re.IGNORECASE)
    skipped = int(skipped_match.group(1)) if skipped_match else len(re.findall(r"\*\*\*Skipped", output))
    failed_match = re.search(r"(\d+) tests? failed out of", output, re.IGNORECASE)
    failed = int(failed_match.group(1)) if failed_match else (1 if result.returncode else 0)
    passed = max(0, registered - failed - skipped)
    status = "passed" if result.returncode == 0 and skipped == 0 else "failed"
    return {"id": suite["id"], "label": label, "status": status, "registered": registered,
            "passed": passed, "failed": failed, "skipped": skipped,
            "duration_seconds": round(time.monotonic() - started, 3),
            "output": output[-12000:] if status != "passed" else ""}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=pathlib.Path, required=True)
    parser.add_argument("--arch", type=int, choices=(32, 64), required=True)
    parser.add_argument("--compiler", choices=("gcc", "clang"), required=True)
    parser.add_argument("--suite", action="append", help="Run only the named required suite")
    parser.add_argument("--dependency", action="append", default=[], metavar="NAME=PATH")
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()

    selected = [s for s in MANIFEST["required_suites"]
                if args.arch in s["arches"] and args.compiler in s["compilers"]
                and (not args.suite or s["id"] in args.suite)]
    if args.suite and set(args.suite) != {s["id"] for s in selected}:
        unknown = sorted(set(args.suite) - {s["id"] for s in selected})
        parser.error(f"suites not required for this configuration: {unknown}")

    dependencies = {name: {"expected_revision": dep["revision"]} for name, dep in MANIFEST["dependencies"].items()}
    for item in args.dependency:
        name, separator, value = item.partition("=")
        if not separator:
            parser.error(f"invalid dependency path: {item}")
        dependencies.setdefault(name, {})["observed_revision"] = git_revision(pathlib.Path(value))

    results = [run_suite(args.build_dir.resolve(), suite) for suite in selected]
    counts = {key: sum(result.get(key, 0) for result in results)
              for key in ("passed", "failed", "skipped")}
    unavailable = sum(result["status"] == "unavailable" for result in results)
    report = {
        "$schema": "release/schemas/evidence.schema.json", "schema_version": 1,
        "simrv": {"version": MANIFEST["version"], "revision": git_revision(ROOT)},
        "host": {"system": platform.platform(), "machine": platform.machine(),
                 "cpu_count": os.cpu_count(), "python": platform.python_version(),
                 "cmake": command_version(["cmake", "--version"]),
                 "compiler": command_version(["c++", "--version"])},
        "dependencies": dependencies,
        "configurations": [{"xlen": args.arch, "misa": f"rv{args.arch}gcbv", "vlen": 256,
                            "compiler": args.compiler, "build_dir": str(args.build_dir), "suites": results}],
        "summary": {**counts, "unavailable": unavailable,
                    "status": "passed" if not counts["failed"] and not counts["skipped"] and not unavailable else "failed"},
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"release evidence: {report['summary']['status']}: {args.output}")
    raise SystemExit(0 if report["summary"]["status"] == "passed" else 1)


if __name__ == "__main__":
    main()
