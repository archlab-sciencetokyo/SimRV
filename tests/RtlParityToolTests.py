#!/usr/bin/env python3
"""Regression tests for the reusable RTL parity adapter framework."""

import pathlib
import subprocess
import sys
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from rtl_parity.common import compare_retirement_traces, parse_retirement_trace
from rtl_parity.registry import get_target, iter_targets


class RtlParityToolTests(unittest.TestCase):
    def test_builtin_targets_are_declarative_and_unique(self):
        targets = list(iter_targets())
        self.assertEqual([target.name for target in targets], ["rvcomp", "cfu-pg"])
        self.assertEqual(len({target.name for target in targets}), len(targets))
        self.assertIs(get_target("rvcomp"), targets[0])
        self.assertIsNone(get_target("unknown"))

    def test_list_targets_does_not_load_external_tools(self):
        result = subprocess.run(
            [sys.executable, str(ROOT / "scripts/evaluate_rtl_parity.py"), "--list-targets"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("rvcomp", result.stdout)
        self.assertIn("cfu-pg", result.stdout)

    def test_trace_parser_accepts_the_adapter_contract(self):
        trace = "CYCLETRACE cycle=17 inst=3 pc=80000004 ir=00000013\n"
        self.assertEqual(parse_retirement_trace(trace), [(17, 3, 0x80000004, 0x13)])

    def test_trace_comparison_reports_event_count_mismatch(self):
        event = "CYCLETRACE cycle=5 inst=1 pc=80000000 ir=00000013\n"
        comparison = compare_retirement_traces(event * 2, event)
        self.assertIsNone(comparison["first_architectural_divergence"])
        self.assertEqual(comparison["first_unpaired_event"], 1)
        self.assertEqual(comparison["event_count_delta"], -1)
        self.assertEqual(comparison["rtl_events"], 2)
        self.assertEqual(comparison["simrv_events"], 1)

    def test_trace_comparison_reports_timing_without_architectural_divergence(self):
        rtl = ("CYCLETRACE cycle=5 inst=1 pc=80000000 ir=00000013\n"
               "CYCLETRACE cycle=8 inst=2 pc=80000004 ir=00000013\n")
        simrv = ("CYCLETRACE cycle=5 inst=1 pc=80000000 ir=00000013\n"
                 "CYCLETRACE cycle=9 inst=2 pc=80000004 ir=00000013\n")
        comparison = compare_retirement_traces(rtl, simrv)
        self.assertIsNone(comparison["first_architectural_divergence"])
        self.assertEqual(comparison["first_timing_divergence"]["index"], 1)


if __name__ == "__main__":
    unittest.main()
