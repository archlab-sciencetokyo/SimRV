#!/usr/bin/env python3
"""Deterministically aggregate benchmark JSON and render Markdown/SVG outputs."""

import argparse
import html
import json
import pathlib
import statistics


def load_rows(paths: list[pathlib.Path]) -> list[dict]:
    rows = []
    for path in sorted(paths):
        report = json.loads(path.read_text(encoding="utf-8"))
        for result in report.get("suite_results", [report]):
            speeds = result["simrv"].get("runs_wall_speed_kips", [])
            rows.append({"source": path.name, "xlen": result["xlen"], "workload": result["test_name"],
                         "samples": len(speeds), "median_kips": statistics.median(speeds) if speeds else 0.0})
    return sorted(rows, key=lambda row: (row["xlen"], row["workload"], row["source"]))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("inputs", nargs="+", type=pathlib.Path)
    parser.add_argument("--json", type=pathlib.Path, required=True)
    parser.add_argument("--table", type=pathlib.Path, required=True)
    parser.add_argument("--plot", type=pathlib.Path, required=True)
    args = parser.parse_args()
    rows = load_rows(args.inputs)
    aggregate = {"schema_version": 1, "statistic": "median", "unit": "KIPS", "results": rows}
    for path in (args.json, args.table, args.plot):
        path.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps(aggregate, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    lines = ["| XLEN | Workload | Samples | Median KIPS |", "| ---: | --- | ---: | ---: |"]
    lines.extend(f"| {r['xlen']} | {r['workload']} | {r['samples']} | {r['median_kips']:.3f} |" for r in rows)
    args.table.write_text("\n".join(lines) + "\n", encoding="utf-8")
    width, row_height = 760, 26
    maximum = max((r["median_kips"] for r in rows), default=1.0)
    svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{50 + row_height * len(rows)}" viewBox="0 0 {width} {50 + row_height * len(rows)}">',
           '<style>text{font:12px sans-serif}.title{font:bold 15px sans-serif}.bar{fill:#4c78a8}</style>',
           '<text class="title" x="10" y="20">SimRV median throughput (KIPS)</text>']
    for index, row in enumerate(rows):
        y = 42 + index * row_height
        label = html.escape(f"RV{row['xlen']} {row['workload']}")
        bar = 450 * row["median_kips"] / maximum
        svg.extend([f'<text x="10" y="{y + 12}">{label}</text>',
                    f'<rect class="bar" x="210" y="{y}" width="{bar:.2f}" height="16"/>',
                    f'<text x="{220 + bar:.2f}" y="{y + 12}">{row["median_kips"]:.3f}</text>'])
    svg.append("</svg>")
    args.plot.write_text("\n".join(svg) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
