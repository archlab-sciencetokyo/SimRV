#!/usr/bin/env python3
"""Exercise lifecycle events and the retained schema-1 retirement compatibility stream."""

import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile


def main() -> int:
    simrv = pathlib.Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="simrv-trace-lifecycle-") as temp:
        root = pathlib.Path(temp)
        guest = root / "guest.bin"
        # Three RISC-V NOPs, executed under the bounded bare-metal CLI.
        guest.write_bytes(bytes.fromhex("130000001300000013000000"))
        retire = root / "retire.jsonl"
        env = os.environ.copy()
        env["NO_COLOR"] = "1"
        result = subprocess.run(
            [
                str(simrv),
                "--cli",
                "--baremetal",
                "--steps",
                "3",
                "-m",
                str(guest),
                "--arch-trace",
                str(retire),
            ],
            capture_output=True,
            text=True,
            env=env,
            timeout=15,
            check=False,
        )
        if result.returncode != 0:
            raise AssertionError(f"SimRV failed ({result.returncode}): {result.stderr}")
        if "Terminal raw mode setup failed" in result.stderr:
            raise AssertionError("non-interactive CLI execution attempted terminal raw mode")
        if "Control+'q'" in result.stdout or "Control+'q'" in result.stderr:
            raise AssertionError("headless CLI banner must not advertise the TUI quit key")
        if "\x1b[" in result.stdout or "\x1b[" in result.stderr:
            raise AssertionError("redirected CLI output must not contain terminal control colors")
        if "Running in headless CLI mode" not in result.stdout:
            raise AssertionError("CLI startup should clearly identify headless execution mode")

        invalid = subprocess.run(
            [str(simrv), "--cli", "--not-a-real-option"],
            capture_output=True,
            text=True,
            env=env,
            timeout=15,
            check=False,
        )
        if invalid.returncode == 0:
            raise AssertionError("an unknown CLI option must fail")
        if "simrv: error: unknown option '--not-a-real-option'" not in invalid.stderr:
            raise AssertionError(f"unknown-option diagnostic is unclear: {invalid.stderr!r}")
        if "\x1b[" in invalid.stderr:
            raise AssertionError("redirected CLI diagnostics must not contain ANSI controls")

        help_result = subprocess.run(
            [str(simrv), "--help"], capture_output=True, text=True, env=env, timeout=15, check=False
        )
        if help_result.returncode != 0:
            raise AssertionError(f"CLI help failed: {help_result.stderr}")
        if "\x1b[" in help_result.stdout:
            raise AssertionError("redirected CLI help must not contain ANSI controls")
        image_help = next(line for line in help_result.stdout.splitlines() if "Load an ELF or raw" in line)
        pc_help = next(line for line in help_result.stdout.splitlines() if "Set the initial program" in line)
        if image_help.index("Load an ELF or raw") != pc_help.index("Set the initial program"):
            raise AssertionError("CLI help option descriptions are not aligned")

        lifecycle = [json.loads(line) for line in (root / "events.jsonl").read_text().splitlines()]
        if not lifecycle or any(event["event"] == "retire" for event in lifecycle):
            raise AssertionError("events.jsonl must contain lifecycle events only")
        expected = {"initialized", "started", "running", "stopped", "completed"}
        actual = {event["event"] for event in lifecycle}
        if not expected.issubset(actual):
            raise AssertionError(f"missing lifecycle transitions: {expected - actual}")
        timestamp_pattern = re.compile(r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d{3}Z$")
        cycles = []
        for event in lifecycle:
            if event.get("schema_version") != 2 or not timestamp_pattern.match(
                event.get("timestamp", "")
            ):
                raise AssertionError(f"invalid lifecycle envelope: {event}")
            cycles.append(event["cycle"])
        if cycles != sorted(cycles):
            raise AssertionError(f"lifecycle cycles are not chronological: {cycles}")

        retirement = [json.loads(line) for line in retire.read_text().splitlines()]
        if not retirement or retirement[0].get("event") != "header":
            raise AssertionError("legacy retirement stream header is missing")
        records = [event for event in retirement[1:] if event.get("event") == "retire"]
        if len(records) != 3 or any(record.get("schema_version") != 1 for record in records):
            raise AssertionError(f"retirement compatibility failed: {records}")
        if any("payload" in record or not record.get("pc") for record in records):
            raise AssertionError("retirement records changed shape or lost their PC")

        # MISA is finalized after Tracer opens the output streams. Ensure the
        # metadata reflects the selected ISA rather than the CPU's default.
        metadata = json.loads((root / "metadata.json").read_text())
        xlen = metadata["xlen"]
        g_trace = root / "g-retire.jsonl"
        g_result = subprocess.run(
            [
                str(simrv), "--cli", "--baremetal", "--steps", "3", "-m", str(guest),
                "--isa", f"rv{xlen}g", "--arch-trace", str(g_trace), "--trace-level", "0",
            ],
            capture_output=True,
            text=True,
            timeout=15,
            check=False,
        )
        if g_result.returncode != 0:
            raise AssertionError(f"G-profile run failed ({g_result.returncode}): {g_result.stderr}")
        g_metadata = json.loads((root / "metadata.json").read_text())
        if g_metadata.get("isa") != f"rv{xlen}g":
            raise AssertionError(f"ISA metadata is stale or noncanonical: {g_metadata.get('isa')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
