#!/usr/bin/env python3
"""Prepare pinned, non-redistributed dependencies for SimRV experiments."""

import argparse
import json
import pathlib
import shutil
import subprocess


ROOT = pathlib.Path(__file__).resolve().parents[1]
MANIFEST = json.loads((ROOT / "release/release-manifest.json").read_text())
GIT_DEPENDENCIES = ("riscv_tests", "vector_tests", "spike")


def run(command: list[str], cwd: pathlib.Path | None = None) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=cwd or ROOT, check=True)


def revision(path: pathlib.Path) -> str:
    return subprocess.run(["git", "-C", str(path), "rev-parse", "HEAD"], text=True,
                          capture_output=True, check=True).stdout.strip()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--directory", type=pathlib.Path, default=ROOT / ".cache/repro")
    parser.add_argument("--check", action="store_true", help="Only verify existing checkouts")
    parser.add_argument("--build-vector-tests", action="store_true")
    args = parser.parse_args()
    args.directory.mkdir(parents=True, exist_ok=True)

    for name in GIT_DEPENDENCIES:
        dependency = MANIFEST["dependencies"][name]
        target = args.directory / name.replace("_", "-")
        if not target.exists():
            if args.check:
                raise SystemExit(f"missing dependency: {target}")
            run(["git", "clone", "--filter=blob:none", dependency["url"], str(target)])
        if not args.check:
            run(["git", "fetch", "--tags", "origin"], target)
            run(["git", "checkout", "--detach", dependency["revision"]], target)
            run(["git", "submodule", "update", "--init", "--recursive"], target)
        observed = revision(target)
        expected = dependency["revision"]
        if len(expected) >= 40 and observed != expected:
            raise SystemExit(f"{name}: expected {expected}, found {observed}")
        print(f"{name}: {observed}")

    vector_dir = args.directory / "vector-tests"
    if args.build_vector_tests and not args.check:
        if not shutil.which("go"):
            raise SystemExit("Go is required to build vector tests")
        run(["make", "-j1"], vector_dir)

    print("Pinned source dependencies are ready. Toolchains and Linux images are built/downloaded separately; see repro/README.md.")


if __name__ == "__main__":
    main()
