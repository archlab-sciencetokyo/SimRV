#!/usr/bin/env python3
"""Validate a SimRV .mission file and, when available, its ELF checkpoint symbols."""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

MISSION_KEYS = {"version", "id", "program", "description", "goal"}
STEP_KEYS = {"id", "title", "prompt", "why", "view", "glossary"}
STEP_KEYS.add("panels")
REQUIRED_STEP_KEYS = {"id", "title", "prompt", "why"}
CHECKPOINT_KEYS = {"label", "after_step", "message"}
VIEWS = {"explain", "registers", "stack", "trace"}
PANELS = VIEWS | {"pipeline", "cache", "tlb", "hazard", "branch-predictor", "bus", "console", "display", "live"}


def parse_mission(path: Path) -> tuple[dict[str, str], list[dict[str, str]], list[dict[str, str | int]]]:
    mission: dict[str, str] = {}
    steps: list[dict[str, str]] = []
    checkpoints: list[dict[str, str | int]] = []
    current: dict[str, str | int] | None = None
    section: str | None = None

    for line_number, raw_line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        line = raw_line.strip()
        if not line or line.startswith("#"):
            continue
        if line in {"[mission]", "[step]", "[checkpoint]"}:
            section = line[1:-1]
            if section == "mission":
                if mission or steps or checkpoints:
                    raise ValueError(f"line {line_number}: [mission] must appear once, first")
                current = mission
            elif section == "step":
                current = {}
                steps.append(current)  # type: ignore[arg-type]
            else:
                current = {}
                checkpoints.append(current)
            continue
        if current is None or section is None:
            raise ValueError(f"line {line_number}: key appears outside a section")
        key, separator, raw_value = line.partition("=")
        if not separator:
            raise ValueError(f"line {line_number}: expected key = value")
        key = key.strip()
        raw_value = raw_value.strip()
        if section == "checkpoint" and key == "after_step":
            if not raw_value.isdecimal() or int(raw_value) < 1:
                raise ValueError(f"line {line_number}: after_step must be a positive integer")
            value: str | int = int(raw_value)
        else:
            try:
                decoded = json.loads(raw_value)
            except json.JSONDecodeError as error:
                raise ValueError(f"line {line_number}: expected a quoted string") from error
            if not isinstance(decoded, str):
                raise ValueError(f"line {line_number}: values must be quoted strings")
            value = decoded
        allowed = MISSION_KEYS if section == "mission" else STEP_KEYS if section == "step" else CHECKPOINT_KEYS
        if key not in allowed:
            raise ValueError(f"line {line_number}: unknown [{section}] key {key!r}")
        if key in current:
            raise ValueError(f"line {line_number}: duplicate key {key!r}")
        current[key] = value

    for key in ("version", "id", "program"):
        if not mission.get(key):
            raise ValueError(f"[mission] requires {key!r}")
    if mission["version"] != "1":
        raise ValueError('unsupported mission version; expected "1"')
    if not steps:
        raise ValueError("mission requires at least one [step]")
    for index, step in enumerate(steps, 1):
        missing = REQUIRED_STEP_KEYS - step.keys()
        if missing:
            raise ValueError(f"[step] {index} is missing: {', '.join(sorted(missing))}")
        if not all(step[key].strip() for key in REQUIRED_STEP_KEYS):
            raise ValueError(f"[step] {index} has an empty required value")
        if "view" not in step and "panels" not in step:
            raise ValueError(f"[step] {index} requires view or panels")
        if "view" in step and step["view"] not in VIEWS:
            raise ValueError(f"[step] {index} has unknown view {step['view']!r}")
        if "panels" in step:
            panels = [panel.strip() for panel in step["panels"].split(",")]
            if not 1 <= len(panels) <= 4 or len(set(panels)) != len(panels) or any(panel not in PANELS for panel in panels):
                raise ValueError(f"[step] {index} panels must contain one to four unique supported views")
        if "glossary" in step and not re.fullmatch(r"[0-5]", step["glossary"]):
            raise ValueError(f"[step] {index} glossary must be an integer string from 0 to 5")

    labels: set[str] = set()
    boundaries: set[int] = set()
    for index, checkpoint in enumerate(checkpoints, 1):
        if not checkpoint.get("label"):
            raise ValueError(f"[checkpoint] {index} requires 'label'")
        if checkpoint["label"] in labels:
            raise ValueError(f"duplicate checkpoint label {checkpoint['label']!r}")
        labels.add(str(checkpoint["label"]))
        after_step = int(checkpoint.get("after_step", 0))
        if after_step < 1 or after_step > len(steps):
            raise ValueError(f"[checkpoint] {index} after_step must refer to an existing step (1-{len(steps)})")
        if after_step in boundaries:
            raise ValueError(f"only one checkpoint may follow step {after_step}")
        boundaries.add(after_step)
    return mission, steps, checkpoints


def find_readelf() -> str | None:
    for candidate in ("readelf", "llvm-readelf", "riscv64-unknown-elf-readelf"):
        found = shutil.which(candidate)
        if found:
            return found
    return None


def elf_symbols(program: Path) -> set[str]:
    tool = find_readelf()
    if not tool:
        raise RuntimeError("no readelf tool found; install binutils or llvm")
    result = subprocess.run([tool, "--syms", "--wide", str(program)], check=False, capture_output=True, text=True)
    if result.returncode:
        detail = result.stderr.strip() or result.stdout.strip()
        raise RuntimeError(f"{tool} could not inspect {program}: {detail}")
    symbols: set[str] = set()
    for row in result.stdout.splitlines():
        columns = row.split()
        if len(columns) >= 8 and columns[0].rstrip(":").isdigit() and columns[6] != "UND":
            symbols.add(columns[7].split("@", 1)[0])
    return symbols


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mission", type=Path, help="mission file to check")
    parser.add_argument("--program", type=Path, help="ELF to check instead of mission['program']")
    args = parser.parse_args()
    try:
        mission, steps, checkpoints = parse_mission(args.mission)
    except (OSError, UnicodeError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1

    print(f"OK: mission {mission['id']!r}, {len(steps)} step(s), {len(checkpoints)} checkpoint(s)")
    program = args.program or (args.mission.parent / mission["program"])
    if not program.is_file():
        print(f"INFO: ELF not found at {program}; checkpoint symbol checks skipped")
        return 0
    try:
        symbols = elf_symbols(program)
    except RuntimeError as error:
        print(f"WARNING: {error}; checkpoint symbol checks skipped", file=sys.stderr)
        return 0
    missing = [str(checkpoint["label"]) for checkpoint in checkpoints if checkpoint["label"] not in symbols]
    if missing:
        print(f"ERROR: checkpoint symbol(s) missing from {program}: {', '.join(missing)}")
        return 1
    print(f"OK: all {len(checkpoints)} checkpoint symbol(s) exist in {program}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
