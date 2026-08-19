#!/usr/bin/env python3
"""Merge partial SimRV evidence reports into one final matrix report."""

import argparse
import json
import pathlib


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("inputs", nargs="+", type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    args = parser.parse_args()
    reports = [json.loads(path.read_text(encoding="utf-8")) for path in sorted(args.inputs)]
    if not reports:
        parser.error("at least one report is required")
    versions = {report["simrv"]["version"] for report in reports}
    revisions = {report["simrv"]["revision"] for report in reports}
    if len(versions) != 1 or len(revisions) != 1:
        raise SystemExit("evidence reports describe different SimRV versions or revisions")
    configurations = []
    dependencies = {}
    for report in reports:
        configurations.extend(report["configurations"])
        for name, value in report.get("dependencies", {}).items():
            dependencies.setdefault(name, {}).update(value)
    configurations.sort(key=lambda c: (c.get("xlen", 0), c.get("compiler", ""),
                                       ",".join(s.get("id", "") for s in c.get("suites", []))))
    suites = [suite for configuration in configurations for suite in configuration.get("suites", [])]
    summary = {key: sum(suite.get(key, 0) for suite in suites) for key in ("passed", "failed", "skipped")}
    summary["unavailable"] = sum(suite.get("status") == "unavailable" for suite in suites)
    summary["status"] = "passed" if not summary["failed"] and not summary["skipped"] and not summary["unavailable"] else "failed"
    merged = {"$schema": "release/schemas/evidence.schema.json", "schema_version": 1,
              "simrv": reports[0]["simrv"], "host": {"system": "multiple CI workers", "machine": "x86_64"},
              "dependencies": dependencies, "configurations": configurations, "summary": summary}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(merged, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(args.output)


if __name__ == "__main__":
    main()
