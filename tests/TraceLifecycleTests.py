#!/usr/bin/env python3
"""Exercise the CLI lifecycle stream and legacy retirement stream together."""

import json
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
            timeout=15,
            check=False,
        )
        if result.returncode != 0:
            raise AssertionError(f"SimRV failed ({result.returncode}): {result.stderr}")

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
