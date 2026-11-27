#!/usr/bin/env python3
"""Regression tests verifying trace directory creation when none exists."""

import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def find_simrv_binary() -> pathlib.Path | None:
    for candidate in [
        ROOT / "build/rv64-release/SimRV",
        ROOT / "build/rv32-release/SimRV",
        ROOT / "build/rv64-debug/SimRV",
        ROOT / "build/rv32-debug/SimRV",
    ]:
        if candidate.is_file():
            return candidate.resolve()
    return None


class TraceDirectoryTests(unittest.TestCase):
    def setUp(self):
        self.simrv = find_simrv_binary()
        if not self.simrv:
            self.skipTest("SimRV binary not built yet")

    def test_trace_snapshot_creates_directory(self):
        with tempfile.TemporaryDirectory() as td:
            cwd = pathlib.Path(td)
            dummy = cwd / "test.bin"
            dummy.write_bytes(b"\x13\x00\x00\x00" * 16)
            self.assertFalse((cwd / "trace").exists())

            res = subprocess.run(
                [str(self.simrv), "--cli", "-m", str(dummy), "-b", "-e", "4", "-r", "0", "3"],
                cwd=cwd,
                capture_output=True,
                text=True,
                timeout=10,
            )
            self.assertEqual(res.returncode, 0, f"SimRV failed: {res.stderr}")
            self.assertTrue((cwd / "trace").is_dir(), "trace directory was not created")
            trace_txt = cwd / "trace" / "trace.txt"
            self.assertTrue(trace_txt.is_file(), "trace/trace.txt was not created")
            self.assertGreater(trace_txt.stat().st_size, 0, "trace/trace.txt is empty")

    def test_instmix_creates_directory(self):
        with tempfile.TemporaryDirectory() as td:
            cwd = pathlib.Path(td)
            dummy = cwd / "test.bin"
            dummy.write_bytes(b"\x13\x00\x00\x00" * 16)
            self.assertFalse((cwd / "trace").exists())

            res = subprocess.run(
                [str(self.simrv), "--cli", "-m", str(dummy), "-b", "-e", "4", "--instmix"],
                cwd=cwd,
                capture_output=True,
                text=True,
                timeout=10,
            )
            self.assertEqual(res.returncode, 0, f"SimRV failed: {res.stderr}")
            self.assertTrue((cwd / "trace").is_dir(), "trace directory was not created")
            instmix_txt = cwd / "trace" / "instmix.txt"
            self.assertTrue(instmix_txt.is_file(), "trace/instmix.txt was not created")
            self.assertGreater(instmix_txt.stat().st_size, 0, "trace/instmix.txt is empty")

    def test_traplog_creates_directory(self):
        with tempfile.TemporaryDirectory() as td:
            cwd = pathlib.Path(td)
            dummy = cwd / "test.bin"
            dummy.write_bytes(b"\x13\x00\x00\x00" * 16)
            self.assertFalse((cwd / "trace").exists())

            res = subprocess.run(
                [str(self.simrv), "--cli", "-m", str(dummy), "-b", "-e", "4", "--trap-log", "trace/traplog.txt"],
                cwd=cwd,
                capture_output=True,
                text=True,
                timeout=10,
            )
            self.assertEqual(res.returncode, 0, f"SimRV failed: {res.stderr}")
            self.assertTrue((cwd / "trace").is_dir(), "trace directory was not created")
            traplog_txt = cwd / "trace" / "traplog.txt"
            self.assertTrue(traplog_txt.is_file(), "trace/traplog.txt was not created")


if __name__ == "__main__":
    unittest.main()
