#!/usr/bin/env python3
"""Tier 4: Real-World Application Scenarios E2E Test Suite for SimRV.

Covers end-to-end, multi-stage application workflows and complete subsystem
executions under realistic operational conditions.
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


class E2ETier4ScenarioTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.simrv64 = get_simrv_binary(64)
        cls.simrv32 = get_simrv_binary(32)
        cls.hello64 = find_example_elf("hello", 64)
        cls.hello32 = find_example_elf("hello", 32)
        cls.dtb = ROOT / "linux-images/rv64/alpine-gui/devicetree.dtb"
        cls.opensbi = ROOT / "linux-images/rv64/alpine-gui/opensbi-linux-payload.elf"
        cls.rootfs = ROOT / "linux-images/rv64/alpine-gui/rootfs.img"

    def test_scenario_1_baremetal_math_and_algorithms(self) -> None:
        """Scenario 1: Comprehensive Baremetal Math & Integer Algorithms.

        Validates arithmetic loops, parity checks, subroutine summation,
        UART printing, and tohost termination.
        """
        res = run_simrv(
            self.simrv64,
            ["--cli", "--baremetal", "-m", str(self.hello64), "--steps", "500000"],
        )
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Hello from SimRV!", res.stdout)
        self.assertIn("Counting: 0 1 2 3 4", res.stdout)
        self.assertIn("sum_to_n(5) = 15", res.stdout)
        self.assertIn("ISA TEST PASS", res.stdout)
        self.assertIn("guest tohost pass", res.stdout)
        self.assertIn("Process exit status      :                      0", res.stdout)

    def test_scenario_2_call_graph_and_stack_frames(self) -> None:
        """Scenario 2: Control Flow & Subroutine Call Graph Verification.

        Validates caller-saved vs callee-saved registers, function calls via JAL,
        16-byte stack frame allocation and release, and return via JALR (ra).
        """
        calls_elf = find_example_elf("calls-stack", 64)
        res_calls = run_simrv(
            self.simrv64,
            ["--cli", "--baremetal", "-m", str(calls_elf), "--steps", "2000"],
        )
        self.assertEqual(res_calls.returncode, 0, res_calls.stderr)
        self.assertIn("Process exit status      :                      0", res_calls.stdout)

        ctrl_elf = find_example_elf("control-flow-calls", 64)
        res_ctrl = run_simrv(
            self.simrv64,
            ["--cli", "--baremetal", "-m", str(ctrl_elf), "--steps", "2000"],
        )
        self.assertEqual(res_ctrl.returncode, 0, res_ctrl.stderr)
        self.assertIn("Process exit status      :                      0", res_ctrl.stdout)

    def test_scenario_3_memory_concurrency_atomics(self) -> None:
        """Scenario 3: Memory Hierarchy & Atomic Operations Under Concurrency.

        Validates multi-hart execution with concurrent atomic instructions (AMO / LR / SC)
        and directory-based MESI cache coherence protocol across private L1 caches.
        """
        atomics_elf = find_example_elf("atomics", 64)
        res = run_simrv(
            self.simrv64,
            [
                "--cli",
                "--baremetal",
                "-m",
                str(atomics_elf),
                "--smp",
                "2",
                "--mode",
                "fast",
                "--steps",
                "3000",
            ],
        )
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("2 hart(s)", res.stdout)
        self.assertIn("Process exit status      :                      0", res.stdout)
        self.assertIn("Hart 0", res.stdout)
        self.assertIn("Hart 1", res.stdout)

    def test_scenario_4_hardware_trap_exception_dispatch(self) -> None:
        """Scenario 4: Hardware Trap & Exception Handling Dispatch.

        Validates exception vectoring, mtvec / mepc / mcause register updates,
        custom trap handler execution, and resumption via MRET.
        """
        traps_elf = find_example_elf("traps", 64)
        res = run_simrv(
            self.simrv64,
            ["--cli", "--baremetal", "-m", str(traps_elf), "--mode", "detailed", "--steps", "2500"],
        )
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Process exit status      :                      0", res.stdout)

    def test_scenario_5_full_headless_simulation_workflow(self) -> None:
        """Scenario 5: Full Headless System Simulation Workflow.

        Executes end-to-end production workflow:
        1. Loads ELF memory image.
        2. Captures full architectural trace in JSON lines format.
        3. Records discrete simulation lifecycle and hardware events.
        4. Saves guest UART transcript to file.
        5. Generates authoritative execution summary JSON.
        Cross-validates consistency across all generated artifacts.
        """
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = pathlib.Path(tmp)
            trace_path = tmp_path / "arch_trace.jsonl"
            events_path = tmp_path / "events.jsonl"
            transcript_path = tmp_path / "uart.txt"
            summary_path = tmp_path / "summary.json"

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
                    "--uart-transcript",
                    str(transcript_path),
                    "--summary",
                    str(summary_path),
                    "--steps",
                    "500000",
                ],
            )
            self.assertEqual(res.returncode, 0, res.stderr)

            # 1. Check UART transcript
            self.assertTrue(transcript_path.is_file())
            uart_content = transcript_path.read_text(encoding="utf-8")
            self.assertIn("Hello from SimRV!", uart_content)
            self.assertIn("sum_to_n(5) = 15", uart_content)

            # 2. Check Execution Summary
            self.assertTrue(summary_path.is_file())
            summary = json.loads(summary_path.read_text(encoding="utf-8"))
            self.assertEqual(summary.get("schema_version"), 1)
            self.assertEqual(summary.get("xlen"), 64)
            self.assertEqual(summary.get("retired_instructions"), 908)
            self.assertEqual(summary.get("exit_status"), 0)

            # 3. Check Lifecycle Events
            self.assertTrue(events_path.is_file())
            events = parse_jsonl(events_path)
            event_types = {e.get("event") for e in events}
            self.assertTrue({"initialized", "started", "running"}.issubset(event_types))

            # 4. Check Architectural Trace
            self.assertTrue(trace_path.is_file())
            trace_records = parse_jsonl(trace_path)
            retires = [r for r in trace_records if r.get("event") == "retire"]
            self.assertEqual(len(retires), 908)
            first_pc = retires[0]["pc"]
            if isinstance(first_pc, str):
                self.assertEqual(int(first_pc, 16), 0x80000000)
            else:
                self.assertEqual(first_pc, 0x80000000)

    def test_scenario_6_full_linux_os_boot_lifecycle(self) -> None:
        """Scenario 6: Full Linux OS Boot & OpenSBI Payload Execution.

        Validates OpenSBI firmware loading, FDT device tree parsing, RAM allocation,
        VirtIO PCIe bus enumeration, and kernel execution commencement.
        """
        if not self.opensbi.is_file() or not self.dtb.is_file():
            self.skipTest("Linux kernel image or DTB not present")

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
                "50000",
            ],
        )
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("OS-mode", res.stdout)
        self.assertIn("pcie platform", res.stdout)
        self.assertTrue(re.search(r"Executed instructions\s*:\s*50\.00K\s*\(50,000\)", res.stdout))
        self.assertIn("Process exit status      :                      0", res.stdout)


if __name__ == "__main__":
    unittest.main()
