#!/usr/bin/env python3
"""Unit tests for the RVA23 ACT qualification runner."""

import pathlib
import subprocess
import sys
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class ActRunnerTests(unittest.TestCase):
    def setUp(self):
        self.runner_script = ROOT / "scripts/run_act_tests.py"
        self.simrv_bin = ROOT / "build/rv64-release/simrv"

    def test_runner_help(self):
        result = subprocess.run(
            [sys.executable, str(self.runner_script), "--help"],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0)
        self.assertIn("RVA23 ACT compliance qualification", result.stdout)

    def test_runner_missing_binary(self):
        result = subprocess.run(
            [sys.executable, str(self.runner_script), "--simrv", "/nonexistent/simrv"],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not exist", result.stderr)

    def test_runner_execution_and_evidence(self):
        if not self.simrv_bin.is_file():
            self.skipTest("simrv binary not built")
        evidence_path = "/tmp/act_test_evidence.json"
        result = subprocess.run(
            [
                sys.executable,
                str(self.runner_script),
                "--simrv",
                str(self.simrv_bin),
                "--output-evidence",
                evidence_path,
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("5/5 passed", result.stdout)
        ev_file = pathlib.Path(evidence_path)
        self.assertTrue(ev_file.is_file())
        ev_file.unlink(missing_ok=True)


if __name__ == "__main__":
    unittest.main()
