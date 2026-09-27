#!/usr/bin/env python3
"""Compare SimRV benchmark evidence; threshold enforcement is explicitly opt-in."""

import argparse
import json
import math
import pathlib
import sys


def load_results(path: pathlib.Path) -> dict[str, float]:
    report = json.loads(path.read_text(encoding="utf-8"))
    if "results" in report:
        return {f"rv{row['xlen']}:{row['workload']}": row["median_kips"]
                for row in report["results"] if row["median_kips"] > 0}
    results = report.get("suite_results", [report])
    return {f"rv{result['xlen']}:{result['test_name']}": result["simrv"]["stats"]["wall_speed"]["median"]
            for result in results if result["simrv"]["stats"]["wall_speed"]["median"] > 0}


def compare(baseline: dict[str, float], candidate: dict[str, float],
            minimum_geomean: float, maximum_regression: float) -> dict:
    names = sorted(baseline.keys() & candidate.keys())
    unmatched = sorted(set(baseline) ^ set(candidate))
    if not names or unmatched:
        raise ValueError(f"benchmark sets differ; unmatched: {unmatched}")
    rows = []
    ratios = []
    for name in names:
        ratio = candidate[name] / baseline[name]
        change = (ratio - 1.0) * 100.0
        ratios.append(ratio)
        rows.append({"benchmark": name, "baseline_kips": baseline[name],
                     "candidate_kips": candidate[name], "change_percent": change,
                     "regression": change < -maximum_regression})
    geomean = (math.exp(sum(math.log(value) for value in ratios) / len(ratios)) - 1.0) * 100.0
    passed = not any(row["regression"] for row in rows) and geomean >= minimum_geomean
    return {"schema_version": 1, "minimum_geomean_percent": minimum_geomean,
            "maximum_regression_percent": maximum_regression,
            "geomean_change_percent": geomean, "passed": passed, "results": rows}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline", type=pathlib.Path)
    parser.add_argument("candidate", type=pathlib.Path)
    parser.add_argument("--minimum-geomean", type=float, default=0.0)
    parser.add_argument("--maximum-regression", type=float, default=3.0)
    parser.add_argument("--json", type=pathlib.Path)
    parser.add_argument("--enforce", action="store_true",
                        help="Return nonzero when supplied thresholds are exceeded")
    args = parser.parse_args()
    try:
        report = compare(load_results(args.baseline), load_results(args.candidate),
                         args.minimum_geomean, args.maximum_regression)
    except ValueError as error:
        print(error, file=sys.stderr)
        raise SystemExit(2) from error
    for row in report["results"]:
        print(f"{row['benchmark']}: {row['change_percent']:+.2f}%")
        if row["regression"]:
            print(f"  regression exceeds {args.maximum_regression:.2f}%", file=sys.stderr)
    print(f"geometric-mean change: {report['geomean_change_percent']:+.2f}%")
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if not report["passed"] and not args.enforce:
        print("threshold observations are informational (evidence-only policy)")
    raise SystemExit(1 if not report["passed"] and args.enforce else 0)


if __name__ == "__main__":
    main()
