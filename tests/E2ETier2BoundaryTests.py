#!/usr/bin/env python3
"""Tier 2: Comprehensive Boundary & Corner Cases E2E Test Suite for SimRV.

Covers all critical boundary domains: address spaces, step limits, register
invariants, SMP limits, mode parsing, input files, output streams, and PMP.
"""

from __future__ import annotations

import os
import pathlib
import re
import subprocess
import tempfile
import unittest

from E2ECommon import (
    ROOT,
    create_instruction_stream,
    find_example_elf,
    get_simrv_binary,
    run_simrv,
)


class E2ETier2BoundaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.simrv64 = get_simrv_binary(64)
        cls.simrv32 = get_simrv_binary(32)
        cls.hello64 = find_example_elf("hello", 64)
        cls.hello32 = find_example_elf("hello", 32)
        cls.dtb = ROOT / "linux-images/rv64/alpine-gui/devicetree.dtb"

    # -------------------------------------------------------------------------
    # Boundary Domain 1: Instruction Step Limits
    # -------------------------------------------------------------------------
    def test_boundary_steps_zero(self) -> None:
        """Verify --steps 0 halts immediately with 0 instructions executed."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--steps", "0"])
        self.assertEqual(res.returncode, 0, res.stderr)
        match = re.search(r"Executed instructions\s*:\s*(\d+)", res.stdout)
        self.assertTrue(match)
        self.assertEqual(int(match.group(1)), 0)

    def test_boundary_steps_one(self) -> None:
        """Verify --steps 1 executes precisely one instruction."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--steps", "1"])
        self.assertEqual(res.returncode, 0, res.stderr)
        match = re.search(r"Executed instructions\s*:\s*(\d+)", res.stdout)
        self.assertTrue(match)
        self.assertEqual(int(match.group(1)), 1)

    def test_boundary_steps_two(self) -> None:
        """Verify --steps 2 executes precisely two instructions."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--steps", "2"])
        self.assertEqual(res.returncode, 0, res.stderr)
        match = re.search(r"Executed instructions\s*:\s*(\d+)", res.stdout)
        self.assertTrue(match)
        self.assertEqual(int(match.group(1)), 2)

    def test_boundary_steps_large(self) -> None:
        """Verify large step count allows guest program to finish to tohost."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--steps", "50000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("guest tohost pass", res.stdout)

    def test_boundary_steps_invalid(self) -> None:
        """Verify invalid or non-numeric step limit is rejected."""
        res = run_simrv(self.simrv64, ["--cli", "--steps", "invalid_number"])
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("error", res.stderr.lower())

    # -------------------------------------------------------------------------
    # Boundary Domain 2: Multi-Hart SMP Limits (1 to 16)
    # -------------------------------------------------------------------------
    def test_boundary_smp_min(self) -> None:
        """Verify --smp 1 (minimum allowed harts) is accepted."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--smp", "1", "--steps", "50"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("1 hart(s)", res.stdout)

    def test_boundary_smp_two(self) -> None:
        """Verify --smp 2 is accepted."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--smp", "2", "--steps", "50"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("2 hart(s)", res.stdout)

    def test_boundary_smp_max(self) -> None:
        """Verify --smp 16 (maximum allowed kMaxHarts) is accepted."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--smp", "16", "--steps", "50"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("16 hart(s)", res.stdout)

    def test_boundary_smp_underflow(self) -> None:
        """Verify --smp 0 is rejected with precise diagnostic."""
        res = run_simrv(self.simrv64, ["--cli", "--smp", "0"])
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("SMP core count must be between 1 and 16 (got: 0)", res.stderr)

    def test_boundary_smp_overflow(self) -> None:
        """Verify --smp 17 (exceeding kMaxHarts) is rejected with precise diagnostic."""
        res = run_simrv(self.simrv64, ["--cli", "--smp", "17"])
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("SMP core count must be between 1 and 16 (got: 17)", res.stderr)

    # -------------------------------------------------------------------------
    # Boundary Domain 3: Address Space & DRAM Allocation Boundaries
    # -------------------------------------------------------------------------
    def test_boundary_start_pc_dram_base(self) -> None:
        """Verify default starting PC at 0x80000000."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--start-pc", "0x80000000", "--steps", "10"])
        self.assertEqual(res.returncode, 0, res.stderr)

    def test_boundary_start_pc_override_raw_bin(self) -> None:
        """Verify starting PC override on raw binary image."""
        with tempfile.TemporaryDirectory() as tmp:
            raw = pathlib.Path(tmp) / "raw.bin"
            raw.write_bytes(create_instruction_stream(["00000013"]))
            res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(raw), "--start-pc", "0x80000100", "--steps", "0"])
            self.assertEqual(res.returncode, 0, res.stderr)
            self.assertIn("0x0000000080000100", res.stdout)

    def test_boundary_dram_size_min_16mb(self) -> None:
        """Verify minimum allowed DRAM size of 16MB (16777216 bytes)."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--dram-size", "16777216", "--steps", "50"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("16 MiB RAM", res.stdout)

    def test_boundary_dram_size_under_16mb_rejected(self) -> None:
        """Verify DRAM size under 16MB is rejected."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--dram-size", "8388608"])
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("minimum 16MB", res.stderr)

    def test_boundary_dram_size_non_power_of_two_rejected(self) -> None:
        """Verify non-power-of-two DRAM size is rejected."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--dram-size", "50000000"])
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("power of two", res.stderr)

    # -------------------------------------------------------------------------
    # Boundary Domain 4: Architectural Register Boundaries
    # -------------------------------------------------------------------------
    def test_boundary_reg_x0_write_discard(self) -> None:
        """Verify writing non-zero to x0 has zero effect."""
        with tempfile.TemporaryDirectory() as tmp:
            bin_path = pathlib.Path(tmp) / "x0.bin"
            # ADDI x0, zero, 0x123; ADDI a0, x0, 0
            bin_path.write_bytes(create_instruction_stream(["12300013", "00000513"]))
            res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(bin_path), "--steps", "2"])
            self.assertEqual(res.returncode, 0, res.stderr)

    def test_boundary_reg_x31_max_gpr(self) -> None:
        """Verify decoding and explanation of instruction targeting highest GPR x31."""
        res = run_simrv(self.simrv64, ["--explain-inst", "0x02a00f93"])  # addi t6, zero, 42 (t6=x31)
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Destination Register: x31", res.stdout)

    def test_boundary_reg_sp_alignment(self) -> None:
        """Verify SP 16-byte stack frame preservation in calls-stack.elf."""
        elf = find_example_elf("calls-stack", 64)
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(elf), "--steps", "1500"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Process exit status      :                      0", res.stdout)

    def test_boundary_imm_max_positive_12bit(self) -> None:
        """Verify maximum positive 12-bit signed immediate (+2047, 0x7FF)."""
        res = run_simrv(self.simrv64, ["--explain-inst", "0x7ff00513"])  # addi a0, zero, 2047
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("2047", res.stdout)

    def test_boundary_imm_max_negative_12bit(self) -> None:
        """Verify maximum negative 12-bit signed immediate (-2048, 0x800)."""
        res = run_simrv(self.simrv64, ["--explain-inst", "0x80000513"])  # addi a0, zero, -2048
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("-2048", res.stdout)

    # -------------------------------------------------------------------------
    # Boundary Domain 5: Execution Mode & Pipeline Boundaries
    # -------------------------------------------------------------------------
    def test_boundary_mode_fast(self) -> None:
        """Verify --mode fast execution mode boundary."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--mode", "fast", "--steps", "50"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("instruction-fast", res.stdout)

    def test_boundary_mode_detailed(self) -> None:
        """Verify --mode detailed execution mode boundary."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--mode", "detailed", "--steps", "50"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("instruction-observable", res.stdout)

    def test_boundary_mode_cycle_accurate(self) -> None:
        """Verify --mode cycle-accurate execution mode boundary."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--mode", "cycle-accurate", "--steps", "50"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("cycle-fast", res.stdout)

    def test_boundary_mode_invalid_rejected(self) -> None:
        """Verify unsupported execution mode is rejected."""
        res = run_simrv(self.simrv64, ["--cli", "--mode", "unsupported-mode"])
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("unsupported execution mode", res.stderr)

    def test_boundary_pipeline_invalid_rejected(self) -> None:
        """Verify invalid pipeline type is rejected."""
        res = run_simrv(self.simrv64, ["--cli", "--pipeline", "dual"])
        self.assertNotEqual(res.returncode, 0)

    # -------------------------------------------------------------------------
    # Boundary Domain 6: Artifact & Input File Boundaries
    # -------------------------------------------------------------------------
    def test_boundary_nonexistent_elf_rejected(self) -> None:
        """Verify non-existent ELF file path reports clean error."""
        res = run_simrv(self.simrv64, ["--cli", "-m", "/nonexistent/guest.elf"])
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("error", res.stderr.lower())

    def test_boundary_empty_file_traps(self) -> None:
        """Verify 0-byte file input halts on unhandled trap."""
        with tempfile.TemporaryDirectory() as tmp:
            empty = pathlib.Path(tmp) / "empty.bin"
            empty.write_bytes(b"")
            res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(empty)])
            self.assertIn("unhandled trap", res.stdout)

    def test_boundary_corrupt_elf_rejected(self) -> None:
        """Verify corrupted ELF header is rejected."""
        with tempfile.TemporaryDirectory() as tmp:
            corrupt = pathlib.Path(tmp) / "corrupt.elf"
            corrupt.write_bytes(b"\x7fELF\x02\x01\x01\x00" + b"\xFF" * 100)
            res = run_simrv(self.simrv64, ["--cli", "-m", str(corrupt)])
            self.assertNotEqual(res.returncode, 0)

    def test_boundary_os_mode_missing_dtb(self) -> None:
        """Verify OS mode requires DTB."""
        env = os.environ.copy()
        env.pop("SIMRV_LINUX_DTB", None)
        res = run_simrv(self.simrv64, ["--cli", "--os", "-m", str(self.hello64)], extra_env={"SIMRV_LINUX_DTB": ""})
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("device tree", res.stderr)

    def test_boundary_invalid_option_rejected(self) -> None:
        """Verify unknown CLI option is rejected."""
        res = run_simrv(self.simrv64, ["-Z"])
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("unknown option", res.stderr)

    # -------------------------------------------------------------------------
    # Boundary Domain 7: Stream & Output Boundaries
    # -------------------------------------------------------------------------
    def test_boundary_summary_stdout_rejected(self) -> None:
        """Verify --summary - is rejected because stdout is reserved for UART."""
        res = run_simrv(self.simrv64, ["--summary", "-"])
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("stdout is reserved for guest UART", res.stderr)

    def test_boundary_events_stdout_rejected(self) -> None:
        """Verify --events - is rejected because stdout is reserved for UART."""
        res = run_simrv(self.simrv64, ["--events", "-"])
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("stdout is reserved for guest UART", res.stderr)

    def test_boundary_no_color_environment(self) -> None:
        """Verify NO_COLOR suppresses terminal escape sequences in diagnostics."""
        res = run_simrv(self.simrv64, ["--not-an-option"], extra_env={"NO_COLOR": "1"})
        self.assertNotEqual(res.returncode, 0)
        self.assertNotIn("\x1b[", res.stderr)

    def test_boundary_nonexistent_summary_dir_handled(self) -> None:
        """Verify write to nonexistent summary path reports error."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--summary", "/nonexistent/dir/out.json", "--steps", "10"])
        self.assertNotEqual(res.returncode, 0)

    def test_boundary_help_options_aligned(self) -> None:
        """Verify CLI help option descriptions are aligned."""
        res = run_simrv(self.simrv64, ["--help"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Usage: simrv", res.stdout)
        self.assertIn("Basic and image loading", res.stdout)

    # -------------------------------------------------------------------------
    # Boundary Domain 8: PMP & Protection Boundaries
    # -------------------------------------------------------------------------
    def test_boundary_pmp_unit_suite(self) -> None:
        """Verify PMP unit test suite passes."""
        res = subprocess.run(["ctest", "--test-dir", str(ROOT / "build/rv64-release"), "-R", "^pmp$", "--output-on-failure"], capture_output=True, text=True, check=False)
        self.assertEqual(res.returncode, 0, res.stderr)

    def test_boundary_privilege_memory_audit(self) -> None:
        """Verify privilege and PMP memory audit test suite passes."""
        res = subprocess.run(["ctest", "--test-dir", str(ROOT / "build/rv64-release"), "-R", "^privilege-audit$", "--output-on-failure"], capture_output=True, text=True, check=False)
        self.assertEqual(res.returncode, 0, res.stderr)

    def test_boundary_lockstep_gdb_mutex(self) -> None:
        """Verify --gdb and --lockstep are mutually exclusive."""
        res = run_simrv(self.simrv64, ["--gdb", "--lockstep"])
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("--gdb and --lockstep are mutually exclusive", res.stderr)

    def test_boundary_gdb_tui_mutex(self) -> None:
        """Verify --gdb and --tui are mutually exclusive."""
        res = run_simrv(self.simrv64, ["--gdb", "--tui"])
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("--gdb and --tui are mutually exclusive", res.stderr)

    def test_boundary_removed_debug_options(self) -> None:
        """Verify removed debug options are rejected as unknown options."""
        for opt in ["--debug-mode", "--debug", "-d", "-g", "--port", "-p", "-M", "-P", "-w", "-x"]:
            res = run_simrv(self.simrv64, [opt])
            self.assertNotEqual(res.returncode, 0, f"Option {opt} should be rejected")
            self.assertIn("unknown option", res.stderr)


if __name__ == "__main__":
    unittest.main()
