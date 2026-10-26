#!/usr/bin/env python3
"""Unified multi-tier test runner for SimRV End-to-End (E2E) test track."""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import sys
import time
import unittest

from E2ECommon import ROOT, get_binary_xlen, get_simrv_binary
from E2ETier1FeatureTests import E2ETier1FeatureTests
from E2ETier2BoundaryTests import E2ETier2BoundaryTests
from E2ETier3CombinationTests import E2ETier3CombinationTests
from E2ETier4ScenarioTests import E2ETier4ScenarioTests


def run_e2e_suites(
    tier: str = "all",
    simrv_bin: pathlib.Path | None = None,
    json_report_path: pathlib.Path | None = None,
    verbosity: int = 1,
) -> int:
    """Run E2E test tiers and optionally generate a JSON report."""
    if simrv_bin:
        simrv_bin = pathlib.Path(simrv_bin).resolve()
        os.environ["SIMRV_BUILD_DIR"] = str(simrv_bin.parent)
        xlen = get_binary_xlen(simrv_bin)
        if xlen == 32:
            os.environ["SIMRV32_BIN"] = str(simrv_bin)
        else:
            os.environ["SIMRV_BIN"] = str(simrv_bin)

    suite = unittest.TestSuite()
    loader = unittest.TestLoader()

    tier_map = {
        "1": [("Tier 1: Feature Coverage", E2ETier1FeatureTests)],
        "2": [("Tier 2: Boundary & Corner Cases", E2ETier2BoundaryTests)],
        "3": [("Tier 3: Cross-Feature Combinations", E2ETier3CombinationTests)],
        "4": [("Tier 4: Real-World Scenarios", E2ETier4ScenarioTests)],
    }

    selected_tiers = []
    if tier == "all":
        for k in ["1", "2", "3", "4"]:
            selected_tiers.extend(tier_map[k])
    elif tier in tier_map:
        selected_tiers.extend(tier_map[tier])
    else:
        print(f"Error: unknown tier '{tier}' (must be 1, 2, 3, 4, or all)", file=sys.stderr)
        return 2

    for name, test_class in selected_tiers:
        test_class.setUpClass()
        tests = loader.loadTestsFromTestCase(test_class)
        suite.addTests(tests)

    print(f"============================================================")
    print(f" SimRV End-to-End (E2E) Test Suite Runner (Tier: {tier})")
    print(f" Binary: {get_simrv_binary(64)}")
    print(f" Total Tests Selected: {suite.countTestCases()}")
    print(f"============================================================")

    start_time = time.monotonic()
    runner = unittest.TextTestRunner(verbosity=verbosity)
    result = runner.run(suite)
    elapsed = time.monotonic() - start_time

    total = result.testsRun
    failures = len(result.failures)
    errors = len(result.errors)
    skipped = len(result.skipped)
    passed = total - (failures + errors + skipped)

    print()
    print("------------------------------------------------------------")
    print(f"E2E Execution Summary:")
    print(f"  Total tests:   {total}")
    print(f"  Passed:        {passed}")
    print(f"  Failures:      {failures}")
    print(f"  Errors:        {errors}")
    print(f"  Skipped:       {skipped}")
    print(f"  Elapsed time:  {elapsed:.3f}s")
    print("------------------------------------------------------------")

    if json_report_path:
        report_data = {
            "tier": tier,
            "simrv_binary": str(get_simrv_binary(64)),
            "total": total,
            "passed": passed,
            "failures": failures,
            "errors": errors,
            "skipped": skipped,
            "elapsed_seconds": elapsed,
            "success": result.wasSuccessful(),
            "failure_details": [
                {"test": str(test), "traceback": trace} for test, trace in result.failures
            ],
            "error_details": [
                {"test": str(test), "traceback": trace} for test, trace in result.errors
            ],
        }
        json_report_path.parent.mkdir(parents=True, exist_ok=True)
        json_report_path.write_text(json.dumps(report_data, indent=2), encoding="utf-8")
        print(f"Report written to: {json_report_path}")

    return 0 if result.wasSuccessful() else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--tier", choices=["1", "2", "3", "4", "all"], default="all", help="Test tier to run")
    parser.add_argument("--simrv", type=pathlib.Path, help="Explicit path to SimRV binary")
    parser.add_argument("--json-report", type=pathlib.Path, help="Write test execution summary JSON report")
    parser.add_argument("-v", "--verbose", action="count", default=1, help="Increase verbosity")
    args = parser.parse_args()

    return run_e2e_suites(
        tier=args.tier,
        simrv_bin=args.simrv,
        json_report_path=args.json_report,
        verbosity=args.verbose,
    )


if __name__ == "__main__":
    sys.exit(main())
