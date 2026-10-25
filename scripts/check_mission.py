#!/usr/bin/env python3
"""Validate a SimRV .mission file and, when available, its ELF checkpoint symbols."""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

MISSION_KEYS = {"version", "id", "program", "description", "goal", "speed_hz"}
STEP_KEYS = {"id", "title", "prompt", "learn", "hint", "why", "view", "panels", "glossary", "speed_hz"}
REQUIRED_STEP_KEYS = {"id", "title", "prompt", "why"}
CHECKPOINT_KEYS = {"label", "after_step", "message"}
VIEWS = {"explain", "registers", "stack", "trace"}
PANELS = VIEWS | {"pipeline", "cache", "tlb", "hazard", "branch-predictor", "bus", "console", "display"}


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
    if "speed_hz" in mission and not valid_speed(mission["speed_hz"]):
        raise ValueError("[mission] speed_hz must be an integer from 0 to 1000000")
    if not steps:
        raise ValueError("mission requires at least one [step]")
    step_ids: set[str] = set()
    for index, step in enumerate(steps, 1):
        missing = REQUIRED_STEP_KEYS - step.keys()
        if missing:
            raise ValueError(f"[step] {index} is missing: {', '.join(sorted(missing))}")
        if not all(step[key].strip() for key in REQUIRED_STEP_KEYS):
            raise ValueError(f"[step] {index} has an empty required value")
        if step["id"] in step_ids:
            raise ValueError(f"[step] {index} duplicates step id {step['id']!r}; assign each step a unique id")
        step_ids.add(step["id"])
        if "view" not in step and "panels" not in step:
            raise ValueError(f"[step] {index} requires view or panels")
        if "view" in step and step["view"] not in VIEWS:
            raise ValueError(f"[step] {index} has unknown view {step['view']!r}")
        if "panels" in step:
            panels = [panel.strip() for panel in step["panels"].split(",")]
            if not 1 <= len(panels) <= 4 or len(set(panels)) != len(panels) or any(panel not in PANELS for panel in panels):
                raise ValueError(f"[step] {index} panels must contain one to four unique supported views")
        if "speed_hz" in step and not valid_speed(step["speed_hz"]):
            raise ValueError(f"[step] {index} speed_hz must be an integer from 0 to 1000000")
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


def valid_speed(value: str) -> bool:
    return bool(re.fullmatch(r"[0-9]+", value)) and int(value) <= 1_000_000


def minimum_width(panel_count: int) -> int:
    return (40, 80, 144, 192)[panel_count - 1]


def print_preview(
    mission: dict[str, str],
    steps: list[dict[str, str]],
    checkpoints: list[dict[str, str | int]],
) -> None:
    default_speed = int(mission.get("speed_hz", "30"))
    boundaries: dict[int, dict[str, str | int]] = {
        int(checkpoint["after_step"]): checkpoint for checkpoint in checkpoints
    }
    print(f"PREVIEW: {mission['id']} — {mission.get('goal', 'No goal specified')}")
    if mission.get("description"):
        print(f"Lesson: {mission['description']}")
    start = 1
    for end in [*sorted(boundaries), len(steps)]:
        if end < start:
            continue
        checkpoint = boundaries.get(end)
        suffix = f" → checkpoint {checkpoint['label']}" if checkpoint else " (no checkpoint boundary)"
        print(f"\nSteps {start}–{end}{suffix}")
        for index in range(start, end + 1):
            step = steps[index - 1]
            panels = [panel.strip() for panel in step["panels"].split(",")] if step.get("panels") else [step["view"]]
            speed = int(step.get("speed_hz", str(default_speed)))
            speed_label = "maximum speed (0 Hz)" if speed == 0 else f"{speed} Hz"
            print(f"  {index}. {step['title']} [{step['id']}] — {speed_label}")
            print(f"     Panels: {', '.join(panels)} (minimum width {minimum_width(len(panels))} columns)")
            learn = step.get("learn") or step["title"]
            print(f"     Learn: {learn}")
            print(f"     Do: {step['prompt']}")
            if step.get("hint"):
                print(f"     Hint: {step['hint']}")
            print(f"     Why: {step['why']}")
        if checkpoint and checkpoint.get("message"):
            print(f"   Reached message: {checkpoint['message']}")
        start = end + 1


def find_readelf() -> str | None:
    for candidate in ("readelf", "llvm-readelf", "riscv64-unknown-elf-readelf"):
        found = shutil.which(candidate)
        if found:
            return found
    return None


def elf_symbols(program: Path) -> set[str]:
    return set(elf_symbol_addresses(program))


def elf_symbol_addresses(program: Path) -> dict[str, int]:
    tool = find_readelf()
    if not tool:
        raise RuntimeError("no readelf tool found; install binutils or llvm")
    result = subprocess.run([tool, "--syms", "--wide", str(program)], check=False, capture_output=True, text=True)
    if result.returncode:
        detail = result.stderr.strip() or result.stdout.strip()
        raise RuntimeError(f"{tool} could not inspect {program}: {detail}")
    symbols: dict[str, int] = {}
    for row in result.stdout.splitlines():
        columns = row.split()
        if len(columns) >= 8 and columns[0].rstrip(":").isdigit() and columns[6] != "UND":
            symbols[columns[7].split("@", 1)[0]] = int(columns[1], 16)
    return symbols


def locate_simrv(explicit: Path | None) -> Path | None:
    if explicit:
        return explicit if explicit.is_file() else None
    root = Path(__file__).resolve().parents[1]
    for candidate in (root / "build/rv64-release/simrv", root / "build/rv32-release/simrv"):
        if candidate.is_file():
            return candidate
    found = shutil.which("simrv")
    return Path(found) if found else None


def smoke_test_checkpoints(
    simrv: Path,
    program: Path,
    checkpoints: list[dict[str, str | int]],
    symbol_addresses: dict[str, int],
    max_instructions: int,
    timeout_seconds: float,
) -> int:
    if not checkpoints:
        print("INFO: no checkpoints to smoke-test")
        return 0
    with tempfile.TemporaryDirectory(prefix="simrv-mission-smoke-") as directory:
        trace_path = Path(directory) / "execution.jsonl"
        command = [
            str(simrv), "--cli", "--baremetal", "--mode", "fast", "-m", str(program),
            "--steps", str(max_instructions), "--arch-trace", str(trace_path),
        ]
        try:
            result = subprocess.run(command, check=False, capture_output=True, text=True,
                                    timeout=timeout_seconds)
        except subprocess.TimeoutExpired:
            print(f"ERROR: checkpoint smoke test timed out after {timeout_seconds:g}s")
            return 1
        except OSError as error:
            print(f"ERROR: could not run {simrv}: {error}")
            return 1
        if not trace_path.is_file():
            detail = result.stderr.strip() or result.stdout.strip() or "no execution trace was written"
            print(f"ERROR: simulator did not produce an architectural trace: {detail}")
            return 1
        retired_pcs: list[int] = []
        with trace_path.open(encoding="utf-8") as trace:
            for line in trace:
                try:
                    event = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if event.get("event") == "retire" and isinstance(event.get("pc"), str):
                    retired_pcs.append(int(event["pc"], 16))

    ordered_checkpoints = sorted(checkpoints, key=lambda checkpoint: int(checkpoint["after_step"]))
    cursor = 0
    failed = False
    print(f"SMOKE TEST: executed {len(retired_pcs)} instruction(s) from {program}")
    for checkpoint in ordered_checkpoints:
        label = str(checkpoint["label"])
        address = symbol_addresses.get(label)
        if address is None:
            print(f"ERROR: checkpoint {label!r} has no address in the ELF symbol table")
            failed = True
            continue
        position = next((index for index in range(cursor, len(retired_pcs))
                         if retired_pcs[index] == address), None)
        if position is None:
            print(f"ERROR: checkpoint {label!r} (after step {checkpoint['after_step']}) was not "
                  f"reached in order; increase --max-instructions or check guest control flow")
            failed = True
            continue
        print(f"OK: checkpoint {label} after step {checkpoint['after_step']} was reached")
        cursor = position
    if result.returncode != 0:
        detail = result.stderr.strip() or result.stdout.strip() or f"exit status {result.returncode}"
        print(f"ERROR: simulator reported an execution failure: {detail}")
        failed = True
    return 1 if failed else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mission", type=Path, help="mission file to check")
    parser.add_argument("--program", type=Path, help="ELF to check instead of mission['program']")
    parser.add_argument("--preview", action="store_true", help="print the validated lesson sequence and layout requirements")
    parser.add_argument("--smoke-test", action="store_true", help="execute the guest and check that checkpoint symbols are reached in boundary order")
    parser.add_argument("--simrv", type=Path, help="simrv executable for --smoke-test (auto-detected by default)")
    parser.add_argument("--max-instructions", type=int, default=100_000, help="instruction cap for --smoke-test (default: 100000)")
    parser.add_argument("--timeout", type=float, default=30.0, help="simulator timeout in seconds for --smoke-test")
    args = parser.parse_args()
    try:
        mission, steps, checkpoints = parse_mission(args.mission)
    except (OSError, UnicodeError, ValueError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1

    print(f"OK: mission {mission['id']!r}, {len(steps)} step(s), {len(checkpoints)} checkpoint(s)")
    if args.preview:
        print_preview(mission, steps, checkpoints)
    program = args.program or (args.mission.parent / mission["program"])
    if not program.is_file():
        print(f"INFO: ELF not found at {program}; checkpoint symbol checks skipped")
        return 1 if args.smoke_test else 0
    try:
        symbol_addresses = elf_symbol_addresses(program)
    except RuntimeError as error:
        print(f"WARNING: {error}; checkpoint symbol checks skipped", file=sys.stderr)
        return 1 if args.smoke_test else 0
    missing = [str(checkpoint["label"]) for checkpoint in checkpoints if checkpoint["label"] not in symbol_addresses]
    if missing:
        print(f"ERROR: checkpoint symbol(s) missing from {program}: {', '.join(missing)}")
        return 1
    print(f"OK: all {len(checkpoints)} checkpoint symbol(s) exist in {program}")
    if args.smoke_test:
        if args.max_instructions < 1 or args.timeout <= 0:
            print("ERROR: --max-instructions and --timeout must be positive", file=sys.stderr)
            return 1
        simrv = locate_simrv(args.simrv)
        if simrv is None:
            print("ERROR: simrv executable not found; pass --simrv <path>", file=sys.stderr)
            return 1
        return smoke_test_checkpoints(simrv, program, checkpoints, symbol_addresses,
                                      args.max_instructions, args.timeout)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
