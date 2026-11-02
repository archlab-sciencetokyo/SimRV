#!/usr/bin/env python3
"""Synchronize the HPM event catalog with the Linux DT and user reference tables."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CATALOG = ROOT / "include/simrv/core/HpmEventCatalog.def"
TEMPLATES = [ROOT / "scripts/templates/virt-rv32.dts", ROOT / "scripts/templates/virt-rv64.dts"]
DOC = ROOT / "docs/hardware/hpm.md"
BEGIN_DTS = "    // BEGIN GENERATED SIMRV HPM CATALOG"
END_DTS = "    // END GENERATED SIMRV HPM CATALOG"
BEGIN_DOC = "<!-- BEGIN GENERATED SIMRV HPM CATALOG -->"
END_DOC = "<!-- END GENERATED SIMRV HPM CATALOG -->"


def catalog() -> tuple[list[tuple[int, str, str, str]], list[tuple[int, int]]]:
    events = []
    generic = []
    event_re = re.compile(r'^SIMRV_HPM_EVENT\((\w+),\s*(\d+),\s*"([^"]+)",\s*"([^"]+)"\)$')
    generic_re = re.compile(r"^SIMRV_HPM_GENERIC_EVENT\((\d+),\s*(\w+)\)$")
    for raw in CATALOG.read_text(encoding="utf-8").splitlines():
        if match := event_re.match(raw.strip()):
            name, ident, label, description = match.groups()
            events.append((int(ident), name, label, description))
        elif match := generic_re.match(raw.strip()):
            generic.append((int(match.group(1)), match.group(2)))
        elif raw.strip():
            raise ValueError(f"invalid HPM catalog entry: {raw}")
    if not events or [row[0] for row in events] != list(range(1, len(events) + 1)):
        raise ValueError("HPM selector IDs must be contiguous and start at one")
    names = {row[1] for row in events}
    if any(name not in names for _, name in generic):
        raise ValueError("generic SBI mappings must refer to a catalog event")
    return events, generic


def dts_block(events: list[tuple[int, str, str, str]], generic: list[tuple[int, int]]) -> str:
    generic_ids = {name: next(ident for ident, event_name, _, _ in events if event_name == name)
                   for _, name in generic}
    lines = [
        "    pmu {",
        '        compatible = "riscv,pmu";',
        "        /* Generic mappings are defined in HpmEventCatalog.def. */",
    ]
    if generic:
        lines.append("        riscv,event-to-mhpmevent =\n" + ",\n".join(
            f"            <0x{code:05x} 0x00000000 0x{generic_ids[name]:08x}>"
            for code, name in generic) + ";")
        lines.append("        riscv,event-to-mhpmcounters =\n" + ",\n".join(
            f"            <0x{code:05x} 0x{code:05x} 0xfffffff8>" for code, _ in generic) + ";")
    lines.append("        /* Raw selector IDs map one-to-one to HPM counters 3..31. */")
    lines.append("        riscv,raw-event-to-mhpmcounters =\n" + ",\n".join(
        f"            <0x00000000 0x{ident:08x} 0xffffffff 0xffffffff 0xfffffff8>"
        for ident, _, _, _ in events) + ";")
    lines.append("    };")
    return "\n".join(lines)


def doc_table(events: list[tuple[int, str, str, str]]) -> str:
    lines = ["| ID | Event | Counting boundary |", "|---:|---|---|"]
    lines.extend(f"| {ident} | {label} | {description} |" for ident, _, label, description in events)
    return "\n".join(lines)


def replace_section(text: str, start: str, end: str, replacement: str) -> str:
    begin = text.index(start) + len(start)
    finish = text.index(end, begin)
    return text[:begin] + "\n" + replacement + "\n" + text[finish:]


def main() -> int:
    parser = argparse.ArgumentParser()
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--write", action="store_true", help="update the DT templates and docs")
    action.add_argument("--check", action="store_true", help="fail if generated files are stale")
    args = parser.parse_args()
    events, generic = catalog()
    generated_block = dts_block(events, generic)
    generated_table = doc_table(events)
    files = [(path, BEGIN_DTS, END_DTS, generated_block) for path in TEMPLATES]
    files.append((DOC, BEGIN_DOC, END_DOC, generated_table))
    stale = []
    for path, start, end, replacement in files:
        text = path.read_text(encoding="utf-8")
        updated = replace_section(text, start, end, replacement)
        if updated != text:
            if args.write:
                path.write_text(updated, encoding="utf-8")
            else:
                stale.append(str(path.relative_to(ROOT)))
    if stale:
        print("HPM catalog outputs are stale: " + ", ".join(stale), file=sys.stderr)
        print("Run scripts/generate_hpm_catalog.py --write", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
