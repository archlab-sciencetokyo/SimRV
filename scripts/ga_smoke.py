#!/usr/bin/env python3
"""Run the small, distribution-independent SimRV GA smoke workflow."""

from __future__ import annotations

import argparse
import json
import pathlib
import subprocess
import sys
import tempfile


def run(binary: pathlib.Path, *args: str) -> str:
    result = subprocess.run(
        [str(binary), *args],
        check=False,
        text=True,
        capture_output=True,
        timeout=20,
    )
    if result.returncode != 0:
        raise RuntimeError(
            f"{binary.name} {' '.join(args)} failed ({result.returncode}):\n"
            f"{result.stdout}{result.stderr}"
        )
    return result.stdout + result.stderr


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--simrv", type=pathlib.Path, required=True)
    parser.add_argument("--guest", type=pathlib.Path)
    args = parser.parse_args()

    binary = args.simrv.resolve()
    if not binary.is_file():
        raise SystemExit(f"simrv binary does not exist: {binary}")

    version = run(binary, "--version")
    capabilities = run(binary, "--isa-info")
    doctor = run(binary, "--doctor")
    if "RV32GCBV" not in capabilities or "RV64GCBV" not in capabilities:
        raise SystemExit("capability report does not advertise both public profiles")
    if "attach transport" not in doctor:
        raise SystemExit("environment diagnostics did not report attach transport")

    with tempfile.TemporaryDirectory(prefix="simrv-ga-smoke-") as directory:
        manifest_path = pathlib.Path(directory) / "soc.json"
        run(binary, "--dump-soc-manifest", "virt-pcie", str(manifest_path))
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        if manifest.get("schema_version") != 1:
            raise SystemExit("unexpected SoC manifest schema version")
        if manifest.get("manifest_version") != "3.0":
            raise SystemExit("SoC manifest is missing the 3.0 compatibility contract")

    if args.guest:
        run(binary, "--cli", "--baremetal", "-m", str(args.guest), "--steps", "100000")

    print("ga-smoke: version, capability, doctor, and SoC manifest checks passed")
    if args.guest:
        print(f"ga-smoke: guest execution passed for {args.guest}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, subprocess.SubprocessError, RuntimeError, json.JSONDecodeError) as error:
        print(f"ga-smoke: {error}", file=sys.stderr)
        raise SystemExit(1)
