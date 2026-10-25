#!/usr/bin/env python3
"""Tier 3: Cross-Feature Combinations E2E Test Suite for SimRV.

Verifies pairwise feature interactions across architectures, pipelines,
devices, and operational interfaces.
"""

from __future__ import annotations

import json
import os
import pathlib
import re
import subprocess
import tempfile
import unittest

from E2ECommon import (
    ROOT,
    find_example_elf,
    get_simrv_binary,
    parse_jsonl,
    run_simrv,
)


class E2ETier3CombinationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.simrv64 = get_simrv_binary(64)
        cls.simrv32 = get_simrv_binary(32)
        cls.hello64 = find_example_elf("hello", 64)
        cls.hello32 = find_example_elf("hello", 32)
        cls.dtb = ROOT / "linux-images/rv64/alpine-gui/devicetree.dtb"
        cls.opensbi = ROOT / "linux-images/rv64/alpine-gui/opensbi-linux-payload.elf"
        cls.rootfs = ROOT / "linux-images/rv64/alpine-gui/rootfs.img"

    def test_comb_1_rv64_smp_baremetal(self) -> None:
        """Combination 1: RV64 Architecture × Multi-Hart SMP (smp=2) × Baremetal Execution."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--smp", "2", "--steps", "2500"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Run configuration: RV64", res.stdout)
        self.assertIn("2 hart(s)", res.stdout)
        self.assertIn("guest tohost pass", res.stdout)

    def test_comb_2_hosted_rv32_fast_execution(self) -> None:
        """Combination 2: RV64 Binary × RV32 Guest Personality × Fast Mode Execution."""
        res = run_simrv(
            self.simrv64,
            ["--cli", "--baremetal", "-m", str(self.hello32), "--isa", "rv32gc", "--mode", "fast", "--steps", "1500"],
        )
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("instruction-fast execution", res.stdout)
        self.assertIn("guest tohost pass", res.stdout)

    def test_comb_3_ca_5stage_cache_pmp(self) -> None:
        """Combination 3: Cycle-Accurate 5-Stage Pipeline × Cache Hierarchy × PMP Platform."""
        res = run_simrv(
            self.simrv64,
            [
                "--cli",
                "--baremetal",
                "-m",
                str(self.hello64),
                "--mode",
                "cycle-accurate",
                "--pipeline",
                "5stage",
                "--config",
                "virt-pcie",
                "--steps",
                "1000",
            ],
        )
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("5-Stage In-Order", res.stdout)
        self.assertIn("L1I hits / misses", res.stdout)
        self.assertIn("L1D hits / misses", res.stdout)

    def test_comb_4_detailed_virtio_uart(self) -> None:
        """Combination 4: Detailed Pipeline Mode × VirtIO Block Device × UART Streaming."""
        with tempfile.TemporaryDirectory() as tmp:
            dummy_disk = pathlib.Path(tmp) / "disk.img"
            dummy_disk.write_bytes(b"\x00" * 4096)
            res = run_simrv(
                self.simrv64,
                [
                    "--cli",
                    "--baremetal",
                    "-m",
                    str(self.hello64),
                    "--mode",
                    "detailed",
                    "-D",
                    str(dummy_disk),
                    "-f",
                    str(self.dtb),
                    "--steps",
                    "500",
                ],
            )
            self.assertEqual(res.returncode, 0, res.stderr)
            self.assertIn("instruction-observable", res.stdout)
            self.assertIn("Hello from SimRV!", res.stdout)

    def test_comb_5_smp_timebase_clint(self) -> None:
        """Combination 5: Multi-Hart SMP (smp=2) × Timebase Clock Frequency × CLINT Timer."""
        res = run_simrv(
            self.simrv64,
            [
                "--cli",
                "--baremetal",
                "-m",
                str(self.hello64),
                "--smp",
                "2",
                "--timebase-hz",
                "10000000",
                "--timer",
                "300",
                "--steps",
                "600",
            ],
        )
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("2 hart(s)", res.stdout)

    def test_comb_6_gdb_headless_conflict(self) -> None:
        """Combination 6: GDB Remote Server Flag × CLI Headless Mode × Mutex Rules."""
        res = run_simrv(self.simrv64, ["--gdb", "--tui"])
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("--gdb and --tui are mutually exclusive", res.stderr)

    def test_comb_7_triple_stream_telemetry(self) -> None:
        """Combination 7: Architectural Tracing × Event Stream × Summary JSON simultaneously."""
        with tempfile.TemporaryDirectory() as tmp:
            trace_path = pathlib.Path(tmp) / "trace.jsonl"
            events_path = pathlib.Path(tmp) / "events.jsonl"
            summary_path = pathlib.Path(tmp) / "summary.json"

            res = run_simrv(
                self.simrv64,
                [
                    "--cli",
                    "--baremetal",
                    "-m",
                    str(self.hello64),
                    "--arch-trace",
                    str(trace_path),
                    "--events",
                    str(events_path),
                    "--summary",
                    str(summary_path),
                    "--steps",
                    "50",
                ],
            )
            self.assertEqual(res.returncode, 0, res.stderr)
            self.assertTrue(trace_path.is_file(), "Missing arch-trace file")
            self.assertTrue(events_path.is_file(), "Missing events file")
            self.assertTrue(summary_path.is_file(), "Missing summary file")

            trace_records = parse_jsonl(trace_path)
            event_records = parse_jsonl(events_path)
            summary_data = json.loads(summary_path.read_text())

            self.assertEqual(len([r for r in trace_records if r.get("event") == "retire"]), 50)
            self.assertTrue(len(event_records) > 0)
            self.assertEqual(summary_data.get("retired_instructions"), 50)

    def test_comb_8_fast_compressed_fp(self) -> None:
        """Combination 8: Fast Mode Execution × Compressed RVC × Floating-Point Extension."""
        elf = find_example_elf("compressed", 64)
        res = run_simrv(
            self.simrv64,
            ["--cli", "--baremetal", "-m", str(elf), "--mode", "fast", "--isa", "rv64gcv", "--steps", "2000"],
        )
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Fetched compressed insns", res.stdout)
        self.assertIn("Process exit status      :                      0", res.stdout)

    def test_comb_9_os_mode_opensbi_virtio_pcie(self) -> None:
        """Combination 9: OS Mode × OpenSBI Payload × VirtIO PCIe Transport × DTB."""
        if not self.opensbi.is_file() or not self.dtb.is_file():
            self.skipTest("Linux kernel/DTB image not available")
        res = run_simrv(
            self.simrv64,
            [
                "--cli",
                "--os",
                "-m",
                str(self.opensbi),
                "-f",
                str(self.dtb),
                "-D",
                str(self.rootfs),
                "--steps",
                "1000",
            ],
        )
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("OS-mode", res.stdout)
        self.assertIn("pcie platform", res.stdout)
        self.assertIn("instruction limit reached", res.stdout)

    def test_comb_10_mission_classroom_guidance(self) -> None:
        """Combination 10: Classroom Mission Guidance × Checker Preview × CLI Conflict."""
        mission_path = ROOT / "examples/isa/lessons/control-flow-calls.mission"
        if not mission_path.is_file():
            self.skipTest("Mission lesson file not available")

        # 1. Check mission preview via checker script
        checker = ROOT / "scripts/check_mission.py"
        res_chk = subprocess.run(
            ["python3", str(checker), str(mission_path), "--preview"],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(res_chk.returncode, 0, res_chk.stderr)
        self.assertIn("control-flow-calls", res_chk.stdout)

        # 2. Check --mission rejects --cli (mutual exclusion constraint)
        res_conflict = run_simrv(self.simrv64, ["--cli", "--mission", str(mission_path)])
        self.assertNotEqual(res_conflict.returncode, 0)
        self.assertIn("--mission requires interactive TUI mode (cannot be used with --cli)", res_conflict.stderr)


if __name__ == "__main__":
    unittest.main()
