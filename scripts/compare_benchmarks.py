#!/usr/bin/env python3
"""Compare SimRV benchmark evidence; enforcement is explicitly opt-in."""

import argparse
import json
import math
import pathlib
import sys


def load_results(path: pathlib.Path) -> dict[str, float]:
    report = json.loads(path.read_text(encoding="utf-8"))
    results = report.get("suite_results", [report])
    speeds = {}
    for result in results:
        speed = result["simrv"]["stats"]["wall_speed"]["median"]
        if speed > 0:
            speeds[f"rv{result['xlen']}:{result['test_name']}"] = speed
    return speeds


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline", type=pathlib.Path)
    parser.add_argument("candidate", type=pathlib.Path)
    parser.add_argument("--minimum-geomean", type=float, default=5.0)
    parser.add_argument("--maximum-regression", type=float, default=3.0)
    parser.add_argument("--enforce", action="store_true", help="Return nonzero when supplied thresholds are exceeded")
    args = parser.parse_args()

    baseline = load_results(args.baseline)
    candidate = load_results(args.candidate)
    names = sorted(baseline.keys() & candidate.keys())
    if not names or set(baseline) != set(candidate):
        missing = sorted(set(baseline) ^ set(candidate))
        print(f"benchmark sets differ; unmatched: {missing}", file=sys.stderr)
        raise SystemExit(2)

    ratios = []
    failed = False
    for name in names:
        change = (candidate[name] / baseline[name] - 1.0) * 100.0
        ratios.append(candidate[name] / baseline[name])
        print(f"{name}: {change:+.2f}%")
        if change < -args.maximum_regression:
            print(f"  regression exceeds {args.maximum_regression:.2f}%", file=sys.stderr)
            failed = True

    geomean = (math.exp(sum(math.log(value) for value in ratios) / len(ratios)) - 1.0) * 100.0
    print(f"geometric-mean change: {geomean:+.2f}%")
    if geomean < args.minimum_geomean:
        print(f"geometric mean is below {args.minimum_geomean:.2f}%", file=sys.stderr)
        failed = True
    if failed and not args.enforce:
        print("threshold observations are informational (evidence-only policy)")
    raise SystemExit(1 if failed and args.enforce else 0)


if __name__ == "__main__":
    main()
