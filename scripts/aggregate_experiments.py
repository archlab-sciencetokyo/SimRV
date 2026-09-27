#!/usr/bin/env python3
"""Deterministically analyze benchmark JSON and render paper tables/plots."""

import argparse
import hashlib
import html
import json
import pathlib
import random
import statistics


def bootstrap_median_ci(samples: list[float], resamples: int = 10_000,
                        confidence: float = 0.95, seed: str = "simrv") -> list[float]:
    """Return a deterministic percentile bootstrap confidence interval for the median."""
    if not samples:
        return [0.0, 0.0]
    if len(samples) == 1:
        return [samples[0], samples[0]]
    generator = random.Random(int(hashlib.sha256(seed.encode()).hexdigest()[:16], 16))
    medians = sorted(
        statistics.median(generator.choices(samples, k=len(samples))) for _ in range(resamples)
    )
    tail = (1.0 - confidence) / 2.0
    low = medians[max(0, round(tail * (resamples - 1)))]
    high = medians[min(resamples - 1, round((1.0 - tail) * (resamples - 1)))]
    return [low, high]


def load_rows(paths: list[pathlib.Path], resamples: int = 10_000,
              confidence: float = 0.95) -> list[dict]:
    rows = []
    for path in sorted(paths):
        report = json.loads(path.read_text(encoding="utf-8"))
        for result in report.get("suite_results", [report]):
            simrv = result["simrv"].get("runs_wall_speed_kips", [])
            spike = result.get("spike", {}).get("runs_wall_speed_kips", [])
            median_simrv = statistics.median(simrv) if simrv else 0.0
            median_spike = statistics.median(spike) if spike else 0.0
            fingerprint = result.get("configuration_fingerprint", "unknown")
            rows.append({
                "source": path.name,
                "configuration_fingerprint": fingerprint,
                "xlen": result["xlen"],
                "workload": path.stem,
                "target": result["test_name"],
                "samples": len(simrv),
                "median_kips": median_simrv,
                "median_kips_ci": bootstrap_median_ci(
                    simrv, resamples, confidence, f"{fingerprint}:{path.stem}:simrv"
                ),
                "cv_percent": result["simrv"].get("stats", {}).get("wall_speed", {}).get("cv", 0.0),
                "spike_median_kips": median_spike,
                "spike_median_kips_ci": bootstrap_median_ci(
                    spike, resamples, confidence, f"{fingerprint}:{path.stem}:spike"
                ),
                "simrv_over_spike": median_simrv / median_spike if median_spike else None,
            })
    return sorted(rows, key=lambda row: (row["xlen"], row["workload"], row["source"]))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("inputs", nargs="+", type=pathlib.Path)
    parser.add_argument("--json", type=pathlib.Path, required=True)
    parser.add_argument("--table", type=pathlib.Path, required=True)
    parser.add_argument("--plot", type=pathlib.Path, required=True)
    parser.add_argument("--bootstrap-resamples", type=int, default=10_000)
    parser.add_argument("--confidence", type=float, default=0.95)
    args = parser.parse_args()
    if args.bootstrap_resamples < 1 or not 0.0 < args.confidence < 1.0:
        parser.error("bootstrap resamples must be positive and confidence must be between 0 and 1")
    rows = load_rows(args.inputs, args.bootstrap_resamples, args.confidence)
    aggregate = {"schema_version": 1, "statistic": "median", "unit": "KIPS",
                 "confidence": args.confidence, "bootstrap_resamples": args.bootstrap_resamples,
                 "results": rows}
    for path in (args.json, args.table, args.plot):
        path.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps(aggregate, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    lines = [
        "| XLEN | Workload | N | SimRV median KIPS (CI) | CV | Spike median KIPS | SimRV/Spike |",
        "| ---: | --- | ---: | ---: | ---: | ---: | ---: |",
    ]
    for row in rows:
        low, high = row["median_kips_ci"]
        ratio = f"{row['simrv_over_spike']:.3f}×" if row["simrv_over_spike"] else "N/A"
        lines.append(f"| {row['xlen']} | {row['workload']} | {row['samples']} | "
                     f"{row['median_kips']:.3f} [{low:.3f}, {high:.3f}] | "
                     f"{row['cv_percent']:.2f}% | {row['spike_median_kips']:.3f} | {ratio} |")
    args.table.write_text("\n".join(lines) + "\n", encoding="utf-8")
    width, row_height = 840, 26
    maximum = max(1.0, max((max(row["median_kips"], row["spike_median_kips"])
                            for row in rows), default=0.0))
    svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{55 + row_height * len(rows)}" viewBox="0 0 {width} {55 + row_height * len(rows)}">',
           '<style>text{font:12px sans-serif}.title{font:bold 15px sans-serif}.simrv{fill:#4c78a8}.spike{fill:#f58518}</style>',
           '<text class="title" x="10" y="20">Median throughput: SimRV (blue) vs Spike (orange)</text>']
    for index, row in enumerate(rows):
        y = 40 + index * row_height
        label = html.escape(f"RV{row['xlen']} {row['workload']}")
        simrv_bar = 480 * row["median_kips"] / maximum
        spike_bar = 480 * row["spike_median_kips"] / maximum
        svg.extend([f'<text x="10" y="{y + 13}">{label}</text>',
                    f'<rect class="simrv" x="230" y="{y}" width="{simrv_bar:.2f}" height="8"/>',
                    f'<rect class="spike" x="230" y="{y + 9}" width="{spike_bar:.2f}" height="8"/>'])
    svg.append("</svg>")
    args.plot.write_text("\n".join(svg) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
