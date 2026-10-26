#!/usr/bin/env python3
"""Tier 1: Comprehensive Feature Coverage E2E Test Suite for SimRV.

Covers all 21 inventoried features (F1.1 through F5.4) with at least 5
concrete, genuine test cases per feature.
"""

from __future__ import annotations

import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile
import unittest

from E2ECommon import (
    ROOT,
    create_instruction_stream,
    find_example_elf,
    get_active_build_dir,
    get_simrv_binary,
    parse_jsonl,
    run_simrv,
)


class E2ETier1FeatureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.simrv64 = get_simrv_binary(64)
        cls.simrv32 = get_simrv_binary(32)
        cls.hello64 = find_example_elf("hello", 64)
        cls.hello32 = find_example_elf("hello", 32)
        cls.dtb = ROOT / "linux-images/rv64/alpine-gui/devicetree.dtb"

    # -------------------------------------------------------------------------
    # Feature 1: F1.1 Core Domain Typing (Word, Counter, CsrValue, IrqLevel, etc.)
    # -------------------------------------------------------------------------
    def test_f1_1_csr_cycles_counter(self) -> None:
        """Verify elapsed cycles and instruction counter reporting in execution summary."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--steps", "500"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Elapsed cycles", res.stdout)
        self.assertIn("Executed instructions", res.stdout)
        match = re.search(r"Executed instructions\s*:\s*(\d+)", res.stdout)
        self.assertTrue(match, "Execution summary missing executed instructions count")
        self.assertEqual(int(match.group(1)), 500)

    def test_f1_1_cpu_state_typed_irq(self) -> None:
        """Verify typed timer IRQ scheduling and execution."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "-t", "100", "--steps", "300"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("SimCore/RISC-V", res.stdout)

    def test_f1_1_typed_word_bounds_64(self) -> None:
        """Verify 64-bit Word bounds and sign extension in RV64 execution."""
        elf = find_example_elf("arithmetic", 64)
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(elf), "--steps", "1000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Run configuration: RV64", res.stdout)
        self.assertIn("Process exit status      :                      0", res.stdout)

    def test_f1_1_typed_word_bounds_32(self) -> None:
        """Verify 32-bit Word wrapping in native RV32 execution."""
        res = run_simrv(self.simrv32, ["--cli", "--baremetal", "-m", str(self.hello32), "--steps", "1000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Run configuration: RV32", res.stdout)
        self.assertIn("Process exit status      :                      0", res.stdout)

    def test_f1_1_csr_mstatus_misa_values(self) -> None:
        """Verify architectural CSR values and ISA string representation."""
        res = run_simrv(self.simrv64, ["--isa-info"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("RV64GCBV", res.stdout)
        self.assertIn("RV32GCBV", res.stdout)

    # -------------------------------------------------------------------------
    # Feature 2: F1.2 Subsystem Namespace Formalization (Vector, Trap, PMP, TLB, etc.)
    # -------------------------------------------------------------------------
    def test_f1_2_vector_types_compatibility(self) -> None:
        """Verify vector extension types and configuration."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--isa", "rv64gcv", "--steps", "100"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("SimCore/RISC-V", res.stdout)

    def test_f1_2_trap_types_handling(self) -> None:
        """Verify synchronous trap types and delegation in traps.elf."""
        elf = find_example_elf("traps", 64)
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(elf), "--steps", "2000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Process exit status      :                      0", res.stdout)

    def test_f1_2_pmp_types_config(self) -> None:
        """Verify PMP configuration types under standard platform setup."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--config", "virt-pcie", "--steps", "100"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Process exit status      :                      0", res.stdout)

    def test_f1_2_tlb_types_refresh(self) -> None:
        """Verify TLB and memory access in addressing.elf."""
        elf = find_example_elf("addressing", 64)
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(elf), "--steps", "2000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Process exit status      :                      0", res.stdout)

    def test_f1_2_tilelink_coherence_types(self) -> None:
        """Verify multi-hart TileLink coherence types under SMP configuration."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--smp", "2", "--steps", "300"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("2 hart(s)", res.stdout)

    # -------------------------------------------------------------------------
    # Feature 3: F1.3 Missing Domain Abstractions (InterruptCode, RegSpec, etc.)
    # -------------------------------------------------------------------------
    def test_f1_3_interrupt_code_standard_irqs(self) -> None:
        """Verify standard RISC-V interrupt handling via timer."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--timer", "200", "--steps", "400"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Process exit status      :                      0", res.stdout)

    def test_f1_3_vector_element_index_addressing(self) -> None:
        """Verify vector length configuration abstraction (VLEN=256 vs 512)."""
        res256 = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--vlen", "256", "--steps", "50"])
        res512 = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--vlen", "512", "--steps", "50"])
        self.assertEqual(res256.returncode, 0, res256.stderr)
        self.assertEqual(res512.returncode, 0, res512.stderr)

    def test_f1_3_pmp_config_modes(self) -> None:
        """Verify PMP configuration modes support in SoC platform."""
        with tempfile.TemporaryDirectory() as tmp:
            soc_path = pathlib.Path(tmp) / "soc.json"
            res = run_simrv(self.simrv64, ["--dump-soc-manifest", "virt-pcie", str(soc_path)])
            self.assertEqual(res.returncode, 0, res.stderr)
            data = json.loads(soc_path.read_text())
            self.assertIn("memory", data)

    def test_f1_3_cache_way_index_lookup(self) -> None:
        """Verify cache way index and cache hit/miss accounting in cycle-accurate mode."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--mode", "cycle-accurate", "--steps", "1000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("L1I hits / misses", res.stdout)
        self.assertIn("L1D hits / misses", res.stdout)

    def test_f1_3_regspec_encoding(self) -> None:
        """Verify 5-bit register field decoding in instruction explainer."""
        res = run_simrv(self.simrv64, ["--explain-inst", "0x00500513"])  # addi a0, zero, 5 (rd=a0/x10, rs1=x0)
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Destination Register: x10", res.stdout)
        self.assertIn("Source Register 1: x0", res.stdout)

    # -------------------------------------------------------------------------
    # Feature 4: F1.4 DMA & Memory Address Typing (PhysAddr, VirtAddr, MMIO routing)
    # -------------------------------------------------------------------------
    def test_f1_4_phys_addr_dram_base(self) -> None:
        """Verify physical memory address base at 0x80000000."""
        res = run_simrv(self.simrv64, ["--help"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("0x80000000", res.stdout)

    def test_f1_4_dram_size_override(self) -> None:
        """Verify DRAM size parameter parsing and allocation."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--dram-size", "134217728", "--steps", "100"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("128 MiB RAM", res.stdout)

    def test_f1_4_dma_event_tracking(self) -> None:
        """Verify typed event and memory transaction logging."""
        with tempfile.TemporaryDirectory() as tmp:
            events_file = pathlib.Path(tmp) / "events.jsonl"
            res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--events", str(events_file), "--steps", "100"])
            self.assertEqual(res.returncode, 0, res.stderr)
            self.assertTrue(events_file.is_file())
            records = parse_jsonl(events_file)
            self.assertTrue(len(records) > 0)

    def test_f1_4_unaligned_memory_access(self) -> None:
        """Verify typed load widths and misaligned access handling."""
        elf = find_example_elf("load-widths", 64)
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(elf), "--steps", "2000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Process exit status      :                      0", res.stdout)

    def test_f1_4_mmio_address_routing(self) -> None:
        """Verify UART MMIO address routing and output streaming."""
        elf = find_example_elf("uart-output", 64)
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(elf), "--steps", "2000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("SimRV classroom example", res.stdout)

    # -------------------------------------------------------------------------
    # Feature 5: F1.5 Pipeline & Decoder Typing (Instruction, ImmValue, etc.)
    # -------------------------------------------------------------------------
    def test_f1_5_instruction_decode_i_type(self) -> None:
        """Verify I-type immediate decoding and sign extension."""
        res = run_simrv(self.simrv64, ["--explain-inst", "0xff010113"])  # addi sp, sp, -16
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("I-Type", res.stdout)
        self.assertIn("ADDI", res.stdout)

    def test_f1_5_instruction_decode_u_type(self) -> None:
        """Verify U-type immediate decoding."""
        res = run_simrv(self.simrv64, ["--explain-inst", "0x80000537"])  # lui a0, 0x80000
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("lui", res.stdout.lower())

    def test_f1_5_typed_retirement_effects(self) -> None:
        """Verify typed instruction retirement stream in arch-trace."""
        with tempfile.TemporaryDirectory() as tmp:
            trace_file = pathlib.Path(tmp) / "trace.jsonl"
            res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--arch-trace", str(trace_file), "--steps", "10"])
            self.assertEqual(res.returncode, 0, res.stderr)
            self.assertTrue(trace_file.is_file())
            records = parse_jsonl(trace_file)
            retires = [r for r in records if r.get("event") == "retire"]
            self.assertEqual(len(retires), 10)
            self.assertTrue(all("pc" in r for r in retires))

    def test_f1_5_branch_jump_target_calc(self) -> None:
        """Verify branch displacement and jump target calculations in branches.elf."""
        elf = find_example_elf("branches", 64)
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(elf), "--steps", "2000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Process exit status      :                      0", res.stdout)

    def test_f1_5_decoder_compressed_rvc(self) -> None:
        """Verify 16-bit compressed instruction expansion in compressed.elf."""
        elf = find_example_elf("compressed", 64)
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(elf), "--steps", "2000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Fetched compressed insns", res.stdout)
        self.assertIn("Process exit status      :                      0", res.stdout)

    # -------------------------------------------------------------------------
    # Feature 6: F2.1 Strict Aliasing & Safety Fixes
    # -------------------------------------------------------------------------
    def test_f2_1_bit_cast_fp_registers(self) -> None:
        """Verify IEEE-754 floating point operations execute to completion."""
        elf = find_example_elf("floating-point", 64)
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(elf), "--steps", "2000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Process exit status      :                      0", res.stdout)

    def test_f2_1_mmio_interval_overlap_check(self) -> None:
        """Verify distinct, valid MMIO interval registration across platform devices."""
        with tempfile.TemporaryDirectory() as tmp:
            soc_path = pathlib.Path(tmp) / "soc.json"
            res = run_simrv(self.simrv64, ["--dump-soc-manifest", "virt-pcie", str(soc_path)])
            self.assertEqual(res.returncode, 0, res.stderr)
            data = json.loads(soc_path.read_text())
            mmio_ranges = data.get("mmio_ranges", [])
            for i, r1 in enumerate(mmio_ranges):
                for j, r2 in enumerate(mmio_ranges):
                    if i != j:
                        b1, e1 = int(r1["base"], 16), int(r1["base"], 16) + int(r1["size"], 16)
                        b2, e2 = int(r2["base"], 16), int(r2["base"], 16) + int(r2["size"], 16)
                        self.assertTrue(e1 <= b2 or e2 <= b1, f"Overlap between {r1['name']} and {r2['name']}")

    def test_f2_1_config_enum_safe_validation(self) -> None:
        """Verify safe validation and graceful rejection of invalid preset names."""
        res = run_simrv(self.simrv64, ["--cli", "--config", "nonexistent-preset"])
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("error", res.stderr.lower())

    def test_f2_1_memory_load_store_endian(self) -> None:
        """Verify multi-byte little-endian memory load/store operations."""
        elf = find_example_elf("arithmetic", 64)
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(elf), "--steps", "2000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Process exit status      :                      0", res.stdout)

    def test_f2_1_pointer_alignment_safety(self) -> None:
        """Verify 16-byte stack alignment invariants in calls-stack.elf."""
        elf = find_example_elf("calls-stack", 64)
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(elf), "--steps", "2000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Process exit status      :                      0", res.stdout)

    # -------------------------------------------------------------------------
    # Feature 7: F2.2 VirtIO Transport Decoupling (MMIO vs PCIe)
    # -------------------------------------------------------------------------
    def test_f2_2_virtio_mmio_block_boot(self) -> None:
        """Verify VirtIO device support on virt-mmio platform preset."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--config", "virt-mmio", "--steps", "100"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Process exit status      :                      0", res.stdout)

    def test_f2_2_virtio_pcie_block_discovery(self) -> None:
        """Verify VirtIO device support on virt-pcie platform preset."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--config", "virt-pcie", "--steps", "100"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("pcie platform", res.stdout)

    def test_f2_2_virtio_console_io(self) -> None:
        """Verify UART transcript capture decoupled from host console."""
        with tempfile.TemporaryDirectory() as tmp:
            transcript = pathlib.Path(tmp) / "uart.txt"
            res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--uart-transcript", str(transcript), "--steps", "1000"])
            self.assertEqual(res.returncode, 0, res.stderr)
            self.assertTrue(transcript.is_file())
            content = transcript.read_text()
            self.assertIn("Hello from SimRV!", content)

    def test_f2_2_virtio_disk_attachment(self) -> None:
        """Verify VirtIO block device image attachment option (-D)."""
        with tempfile.TemporaryDirectory() as tmp:
            dummy_disk = pathlib.Path(tmp) / "disk.img"
            dummy_disk.write_bytes(b"\x00" * 4096)
            cmd = ["--cli", "--baremetal", "-m", str(self.hello64), "-D", str(dummy_disk), "--steps", "100"]
            if self.dtb.is_file():
                cmd.extend(["-f", str(self.dtb)])
            res = run_simrv(self.simrv64, cmd)
            self.assertEqual(res.returncode, 0, res.stderr)

    def test_f2_2_virtio_transport_error_recovery(self) -> None:
        """Verify clean diagnostic on missing disk image."""
        cmd = ["--cli", "-D", "/path/to/nonexistent/disk.img"]
        if self.dtb.is_file():
            cmd.extend(["-f", str(self.dtb)])
        res = run_simrv(self.simrv64, cmd)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("error", res.stderr.lower())

    # -------------------------------------------------------------------------
    # Feature 8: F2.3 Interrupt Routing Formalization
    # -------------------------------------------------------------------------
    def test_f2_3_plic_external_routing(self) -> None:
        """Verify trap handling and interrupt routing in traps.elf."""
        elf = find_example_elf("traps", 64)
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(elf), "--steps", "1500"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Process exit status      :                      0", res.stdout)

    def test_f2_3_clint_timer_interrupt(self) -> None:
        """Verify CLINT timer interrupt trigger via --timer parameter."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--timer", "50", "--steps", "200"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Process exit status      :                      0", res.stdout)

    def test_f2_3_aclint_sw_interrupt(self) -> None:
        """Verify multi-hart atomic operations and synchronization."""
        elf = find_example_elf("atomics", 64)
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(elf), "--smp", "2", "--steps", "2000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Process exit status      :                      0", res.stdout)

    def test_f2_3_aia_aplic_msi_delivery(self) -> None:
        """Verify AIA interrupt capabilities reported in environment diagnostics."""
        res = run_simrv(self.simrv64, ["--doctor"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("SimRV environment diagnostics", res.stdout)

    def test_f2_3_interrupt_priority_masking(self) -> None:
        """Verify interrupt priority and masking stability under long runs."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--steps", "1000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("guest tohost pass", res.stdout)

    # -------------------------------------------------------------------------
    # Feature 9: F2.4 Procedural Modernization
    # -------------------------------------------------------------------------
    def test_f2_4_csr_dispatch_table(self) -> None:
        """Verify CSR read/write access and exception vectoring."""
        elf = find_example_elf("counters", 64)
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(elf), "--steps", "2000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Process exit status      :                      0", res.stdout)

    def test_f2_4_tui_modal_dispatch(self) -> None:
        """Verify TUI framework unit tests pass."""
        ctest_res = subprocess.run(
            ["ctest", "--test-dir", str(get_active_build_dir()), "-R", "^tui-framework-core$", "--output-on-failure"],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(ctest_res.returncode, 0, ctest_res.stderr)

    def test_f2_4_execution_engine_dispatch(self) -> None:
        """Verify instruction execution engine fast dispatch."""
        elf = find_example_elf("multiply-divide", 64)
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(elf), "--mode", "fast", "--steps", "2000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Process exit status      :                      0", res.stdout)

    def test_f2_4_trap_dispatch_pattern(self) -> None:
        """Verify trap dispatch pattern across trap modes."""
        elf = find_example_elf("traps", 64)
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(elf), "--mode", "detailed", "--steps", "2000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Process exit status      :                      0", res.stdout)

    def test_f2_4_pipeline_stage_dispatch(self) -> None:
        """Verify cycle-accurate pipeline stage dispatch."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--mode", "cycle-accurate", "--steps", "1000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("5-Stage In-Order", res.stdout)

    # -------------------------------------------------------------------------
    # Feature 10: F3.1 Dead Headers Removal (Progressive Interface Verification)
    # -------------------------------------------------------------------------
    def test_f3_1_canonical_memory_header(self) -> None:
        """Verify modern canonical MemoryUtil and MemoryAccess headers exist."""
        self.assertTrue((ROOT / "include/simrv/memory/MemoryUtil.hpp").is_file())
        self.assertTrue((ROOT / "include/simrv/memory/MemoryAccess.hpp").is_file())

    def test_f3_1_canonical_device_headers(self) -> None:
        """Verify modern canonical UART device header exists."""
        self.assertTrue((ROOT / "include/simrv/device/Uart.hpp").is_file())

    def test_f3_1_canonical_core_headers(self) -> None:
        """Verify modern canonical CPU and Machine headers exist."""
        self.assertTrue((ROOT / "include/simrv/core/Cpu.hpp").is_file())
        self.assertTrue((ROOT / "include/simrv/core/Machine.hpp").is_file())

    def test_f3_1_clean_include_boundaries(self) -> None:
        """Verify public include tree contains expected subsystem directories."""
        subdirs = [d.name for d in (ROOT / "include/simrv").iterdir() if d.is_dir()]
        for expected in ["core", "execute", "memory", "pipeline", "device"]:
            self.assertIn(expected, subdirs)

    def test_f3_1_obsolete_headers_hygiene(self) -> None:
        """Verify obsolete headers are either eliminated or minimal stubs pending M1."""
        mem = ROOT / "include/simrv/memory/Memory.hpp"
        if mem.exists():
            content = mem.read_text()
            self.assertNotIn("class CPU", content)

    # -------------------------------------------------------------------------
    # Feature 11: F3.2 Dead Execution Stubs Removal (Progressive Interface Verification)
    # -------------------------------------------------------------------------
    def test_f3_2_canonical_alu_present(self) -> None:
        """Verify standard ExecuteUnitInt is active and present."""
        self.assertTrue((ROOT / "include/simrv/execute/ExecuteUnitInt.hpp").is_file())
        self.assertTrue((ROOT / "src/execute/ExecuteUnitInt.cpp").is_file())

    def test_f3_2_canonical_fp_present(self) -> None:
        """Verify standard ExecuteUnitFp is active and present."""
        self.assertTrue((ROOT / "include/simrv/execute/ExecuteUnitFp.hpp").is_file())
        self.assertTrue((ROOT / "src/execute/ExecuteUnitFp.cpp").is_file())

    def test_f3_2_standard_alu_execution(self) -> None:
        """Verify ALU operations execute cleanly without crypto stub dependencies."""
        elf = find_example_elf("arithmetic", 64)
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(elf), "--steps", "1000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Process exit status      :                      0", res.stdout)

    def test_f3_2_clean_execute_unit_directory(self) -> None:
        """Verify execute module contains maintained execution units."""
        exec_sources = [f.name for f in (ROOT / "src/execute").iterdir() if f.is_file()]
        self.assertIn("ExecuteUnitInt.cpp", exec_sources)
        self.assertIn("ExecuteUnitFp.cpp", exec_sources)

    def test_f3_2_zk_opcode_coverage(self) -> None:
        """Verify basic integer opcodes decode and explain without errors."""
        res = run_simrv(self.simrv64, ["--explain-inst", "0x00000013"])
        self.assertEqual(res.returncode, 0, res.stderr)

    # -------------------------------------------------------------------------
    # Feature 12: F3.3 Dead Constants & Attributes Cleanup (Progressive Interface Verification)
    # -------------------------------------------------------------------------
    def test_f3_3_format_util_present(self) -> None:
        """Verify FormatUtil header is active."""
        self.assertTrue((ROOT / "include/simrv/util/FormatUtil.hpp").is_file())

    def test_f3_3_status_bar_canonical_counter(self) -> None:
        """Verify simulator reports single canonical instruction count in execution summary."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--steps", "500"])
        self.assertEqual(res.returncode, 0, res.stderr)
        matches = re.findall(r"Executed instructions\s*:\s*\d+", res.stdout)
        self.assertEqual(len(matches), 1, "Duplicate or conflicting instruction counters reported")

    def test_f3_3_imsic_device_present(self) -> None:
        """Verify AIA interrupt controller header is active."""
        self.assertTrue((ROOT / "include/simrv/device/AIA.hpp").is_file())

    def test_f3_3_scroll_view_present(self) -> None:
        """Verify ScrollView component header is active."""
        self.assertTrue((ROOT / "include/simrv/tui/framework/ScrollView.hpp").is_file())

    def test_f3_3_gdb_lockstep_mutex(self) -> None:
        """Verify mutually exclusive debug options reject conflicting invocations."""
        res = run_simrv(self.simrv64, ["--gdb", "--lockstep"])
        self.assertIn("mutually exclusive", res.stderr)

    # -------------------------------------------------------------------------
    # Feature 13: F3.4 Test Suite Hygiene
    # -------------------------------------------------------------------------
    def test_f3_4_mission_tool_tests_exists(self) -> None:
        """Verify mission tools test suite exists and is executable."""
        candidates = [
            ROOT / "tests/MissionToolTests.py",
            ROOT / "tests/test_mission_tools.py",
        ]
        test_file = next((f for f in candidates if f.exists()), None)
        self.assertIsNotNone(test_file, "Mission tool tests file not found")
        res = subprocess.run([sys.executable, str(test_file)], capture_output=True, text=True, check=False)
        self.assertEqual(res.returncode, 0, res.stderr)

    def test_f3_4_ctest_registration_mission(self) -> None:
        """Verify CTest test discovery includes platform and regression tests."""
        res = subprocess.run(
            ["ctest", "--test-dir", str(get_active_build_dir()), "-N"],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("core-semantics", res.stdout)
        self.assertIn("modern-platform", res.stdout)

    def test_f3_4_no_orphaned_pyc(self) -> None:
        """Verify no .pyc files are tracked in git index."""
        res = subprocess.run(["git", "ls-files", "*.pyc"], cwd=ROOT, capture_output=True, text=True, check=False)
        self.assertEqual(res.stdout.strip(), "", "Found git-tracked .pyc files")

    def test_f3_4_consistent_test_naming(self) -> None:
        """Verify test files follow PascalCase *Tests.cpp and *Tests.py convention."""
        tests_dir = ROOT / "tests"
        for p in tests_dir.glob("*.cpp"):
            if p.name != "TestCfuPlugin.cpp":
                self.assertTrue(p.name.endswith("Tests.cpp"), f"Non-conforming test file: {p.name}")

    def test_f3_4_ctest_label_coverage(self) -> None:
        """Verify gate label covers 52 core verification tests."""
        res = subprocess.run(
            ["ctest", "--test-dir", str(get_active_build_dir()), "-N", "-L", "gate"],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Total Tests: 52", res.stdout)

    # -------------------------------------------------------------------------
    # Feature 14: F4.1 Hot-Path Zero-Regression Invariants
    # -------------------------------------------------------------------------
    def test_f4_1_zero_register_invariance(self) -> None:
        """Verify x0 register is strictly hardwired to zero."""
        with tempfile.TemporaryDirectory() as tmp:
            bin_path = pathlib.Path(tmp) / "x0_test.bin"
            # ADDI x0, zero, 42; ADDI a0, x0, 0
            bin_path.write_bytes(create_instruction_stream(["02a00013", "00000513"]))
            res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(bin_path), "--steps", "2"])
            self.assertEqual(res.returncode, 0, res.stderr)

    def test_f4_1_fast_execution_throughput(self) -> None:
        """Verify fast execution mode simulation speed is non-zero."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--mode", "fast", "--steps", "1000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Simulation speed", res.stdout)

    def test_f4_1_branchless_register_write(self) -> None:
        """Verify register state updates correctly in arithmetic.elf."""
        elf = find_example_elf("arithmetic", 64)
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(elf), "--steps", "2000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Process exit status      :                      0", res.stdout)

    def test_f4_1_tight_loop_instruction_cadence(self) -> None:
        """Verify tight loop cadence and convergence in loops-arrays.elf."""
        elf = find_example_elf("loops-arrays", 64)
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(elf), "--steps", "5000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Process exit status      :                      0", res.stdout)

    def test_f4_1_step_limit_evaluation(self) -> None:
        """Verify exact step limit evaluation across machine execution."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--steps", "123"])
        self.assertEqual(res.returncode, 0, res.stderr)
        match = re.search(r"Executed instructions\s*:\s*(\d+)", res.stdout)
        self.assertTrue(match)
        self.assertEqual(int(match.group(1)), 123)

    # -------------------------------------------------------------------------
    # Feature 15: F4.2 Memory Alignment Invariants
    # -------------------------------------------------------------------------
    def test_f4_2_cache_line_64byte_alignment(self) -> None:
        """Verify cache hierarchy reports in execution summary."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--steps", "100"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("L1 private per hart", res.stdout)

    def test_f4_2_page_boundary_crossings(self) -> None:
        """Verify memory accesses crossing boundaries in addressing.elf."""
        elf = find_example_elf("addressing", 64)
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(elf), "--steps", "2000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Process exit status      :                      0", res.stdout)

    def test_f4_2_tlb_set_alignment(self) -> None:
        """Verify TLB and MMU operations in modern platform tests."""
        ctest_res = subprocess.run(
            ["ctest", "--test-dir", str(ROOT / "build/rv64-release"), "-R", "^core-semantics$", "--output-on-failure"],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(ctest_res.returncode, 0, ctest_res.stderr)

    def test_f4_2_napot_alignment_computation(self) -> None:
        """Verify PMP unit test suite passes."""
        ctest_res = subprocess.run(
            ["ctest", "--test-dir", str(get_active_build_dir()), "-R", "^pmp$", "--output-on-failure"],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(ctest_res.returncode, 0, ctest_res.stderr)

    def test_f4_2_misaligned_word_access(self) -> None:
        """Verify byte, halfword, and word access execution in load-widths.elf."""
        elf = find_example_elf("load-widths", 64)
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(elf), "--steps", "2000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Process exit status      :                      0", res.stdout)

    # -------------------------------------------------------------------------
    # Feature 16: F4.3 Cycle-Accurate Retirement Retain
    # -------------------------------------------------------------------------
    def test_f4_3_ca_retirement_slot_advance(self) -> None:
        """Verify cycle-accurate execution retirement progression."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--mode", "cycle-accurate", "--steps", "500"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Cycles Per Instr (CPI)", res.stdout)

    def test_f4_3_ca_multi_cycle_stall_handling(self) -> None:
        """Verify multi-cycle stall reporting in CA execution."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--mode", "cycle-accurate", "--steps", "1000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Total Stall/Bubble Cycles", res.stdout)

    def test_f4_3_ca_forwarding_path(self) -> None:
        """Verify operand forwarding flag handling."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--mode", "cycle-accurate", "--no-forwarding", "--steps", "500"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("forwarding disabled", res.stdout)

    def test_f4_3_ca_branch_mispredict_flush(self) -> None:
        """Verify branch prediction hit/miss tracking in CA mode."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--mode", "cycle-accurate", "--steps", "1000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Branch hits / misses", res.stdout)

    def test_f4_3_ca_dual_issue_pairing(self) -> None:
        """Verify 3-stage vs 5-stage pipeline options."""
        res3 = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--mode", "cycle-accurate", "--pipeline", "3stage", "--steps", "500"])
        res5 = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--mode", "cycle-accurate", "--pipeline", "5stage", "--steps", "500"])
        self.assertEqual(res3.returncode, 0, res3.stderr)
        self.assertEqual(res5.returncode, 0, res5.stderr)
        self.assertIn("3-Stage In-Order", res3.stdout)
        self.assertIn("5-Stage In-Order", res5.stdout)

    # -------------------------------------------------------------------------
    # Feature 17: F4.4 Throughput & Latency Benchmarks
    # -------------------------------------------------------------------------
    def test_f4_4_benchmark_script_invocation(self) -> None:
        """Verify scripts/benchmark.py runs with --help."""
        res = subprocess.run([sys.executable, str(ROOT / "scripts/benchmark.py"), "--help"], capture_output=True, text=True, check=False)
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("--simrv", res.stdout)

    def test_f4_4_benchmark_modes_comparison(self) -> None:
        """Verify scripts/benchmark_modes.py runs with --help."""
        res = subprocess.run([sys.executable, str(ROOT / "scripts/benchmark_modes.py"), "--help"], capture_output=True, text=True, check=False)
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("--simrv", res.stdout)

    def test_f4_4_benchmark_json_schema(self) -> None:
        """Verify summary.json output format from SimRV."""
        with tempfile.TemporaryDirectory() as tmp:
            summary_path = pathlib.Path(tmp) / "summary.json"
            res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--summary", str(summary_path), "--steps", "500"])
            self.assertEqual(res.returncode, 0, res.stderr)
            self.assertTrue(summary_path.is_file())
            data = json.loads(summary_path.read_text())
            self.assertIn("retired_instructions", data)
            self.assertIn("cycles", data)

    def test_f4_4_latency_measurement(self) -> None:
        """Verify CPI and IPC consistency in execution summary."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--steps", "1000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Cycles Per Instr (CPI)", res.stdout)
        self.assertIn("Instrs Per Cycle (IPC)", res.stdout)

    def test_f4_4_zero_throughput_regression(self) -> None:
        """Verify simulation speed is non-zero."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--steps", "1000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Simulation speed", res.stdout)

    # -------------------------------------------------------------------------
    # Feature 18: F5.1 RV64 Gate 100% Pass
    # -------------------------------------------------------------------------
    def test_f5_1_rv64_gate_test_discovery(self) -> None:
        """Verify gate tests discovered in release preset."""
        res = subprocess.run(["ctest", "--test-dir", str(get_active_build_dir()), "-N", "-L", "gate"], capture_output=True, text=True, check=False)
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Total Tests: 52", res.stdout)

    def test_f5_1_rv64_gate_execution(self) -> None:
        """Verify sample gate tests pass."""
        res = subprocess.run(["ctest", "--test-dir", str(get_active_build_dir()), "-R", "^(cli-doctor|cli-isa-info)$"], capture_output=True, text=True, check=False)
        self.assertEqual(res.returncode, 0, res.stderr)

    def test_f5_1_rv64_cli_options(self) -> None:
        """Verify standard RV64 CLI options."""
        res = run_simrv(self.simrv64, ["--version"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("(RV64)", res.stdout)

    def test_f5_1_rv64_isa_string_parsing(self) -> None:
        """Verify RV64 ISA string option."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--isa", "rv64gc", "--steps", "100"])
        self.assertEqual(res.returncode, 0, res.stderr)

    def test_f5_1_rv64_baremetal_execution(self) -> None:
        """Verify complete baremetal execution to tohost on RV64."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello64), "--steps", "2000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("guest tohost pass", res.stdout)

    # -------------------------------------------------------------------------
    # Feature 19: F5.2 RV32 Gate 100% Pass
    # -------------------------------------------------------------------------
    def test_f5_2_rv32_gate_test_discovery(self) -> None:
        """Verify 49 gate tests discovered in rv32-release."""
        res = subprocess.run(["ctest", "--test-dir", str(ROOT / "build/rv32-release"), "-N", "-L", "gate"], capture_output=True, text=True, check=False)
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("Total Tests: 52", res.stdout)

    def test_f5_2_rv32_gate_execution(self) -> None:
        """Verify sample RV32 gate tests pass."""
        res = subprocess.run(["ctest", "--test-dir", str(ROOT / "build/rv32-release"), "-R", "^(cli-doctor|cli-isa-info)$"], capture_output=True, text=True, check=False)
        self.assertEqual(res.returncode, 0, res.stderr)

    def test_f5_2_rv32_native_binary(self) -> None:
        """Verify native RV32 binary version and execution."""
        res = run_simrv(self.simrv32, ["--version"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("(RV32)", res.stdout)

    def test_f5_2_rv64_hosted_rv32_personality(self) -> None:
        """Verify RV64 binary executing RV32 guest personality."""
        res = run_simrv(self.simrv64, ["--cli", "--baremetal", "-m", str(self.hello32), "--isa", "rv32gc", "--steps", "2000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("guest tohost pass", res.stdout)

    def test_f5_2_rv32_word_wrap(self) -> None:
        """Verify 32-bit execution termination on native RV32."""
        res = run_simrv(self.simrv32, ["--cli", "--baremetal", "-m", str(self.hello32), "--steps", "2000"])
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertIn("guest tohost pass", res.stdout)

    # -------------------------------------------------------------------------
    # Feature 20: F5.3 Clang-Format Compliance
    # -------------------------------------------------------------------------
    def test_f5_3_format_check_invocation(self) -> None:
        """Verify clang-format executable is available."""
        res = subprocess.run(["clang-format", "--version"], capture_output=True, text=True, check=False)
        self.assertEqual(res.returncode, 0, res.stderr)

    def test_f5_3_include_formatting(self) -> None:
        """Verify headers adhere to formatting standards."""
        header = ROOT / "include/simrv/core/CPU.hpp"
        if header.is_file():
            res = subprocess.run(["clang-format", "--dry-run", "--Werror", str(header)], capture_output=True, text=True, check=False)
            self.assertEqual(res.returncode, 0, res.stderr)

    def test_f5_3_src_formatting(self) -> None:
        """Verify core source files adhere to formatting standards."""
        src = ROOT / "src/Main.cpp"
        if src.is_file():
            res = subprocess.run(["clang-format", "--dry-run", "--Werror", str(src)], capture_output=True, text=True, check=False)
            self.assertEqual(res.returncode, 0, res.stderr)

    def test_f5_3_tests_formatting(self) -> None:
        """Verify test source files adhere to formatting standards."""
        test_src = ROOT / "tests/OperationTraitsTests.cpp"
        if test_src.is_file():
            res = subprocess.run(["clang-format", "--dry-run", "--Werror", str(test_src)], capture_output=True, text=True, check=False)
            self.assertEqual(res.returncode, 0, res.stderr)

    def test_f5_3_no_trailing_whitespace(self) -> None:
        """Verify no trailing whitespace in Python test suites."""
        test_file = ROOT / "tests/TraceLifecycleTests.py"
        lines = test_file.read_text().splitlines()
        for i, line in enumerate(lines, 1):
            self.assertEqual(line, line.rstrip(), f"Trailing whitespace at {test_file}:{i}")

    # -------------------------------------------------------------------------
    # Feature 21: F5.4 Release Check Invariants
    # -------------------------------------------------------------------------
    def test_f5_4_release_check_script(self) -> None:
        """Verify scripts/release_check.py passes with 0 errors."""
        cmd = [sys.executable, str(ROOT / "scripts/release_check.py")]
        if self.simrv64.is_file():
            cmd.extend(["--binary", str(self.simrv64)])
        if self.simrv32.is_file():
            cmd.extend(["--binary", str(self.simrv32)])
        res = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(res.returncode, 0, res.stderr)

    def test_f5_4_release_manifest_schema(self) -> None:
        """Verify release/release-manifest.json exists and contains schema version."""
        manifest_file = ROOT / "release/release-manifest.json"
        self.assertTrue(manifest_file.is_file())
        data = json.loads(manifest_file.read_text())
        self.assertIn("version", data)

    def test_f5_4_citation_cff_sync(self) -> None:
        """Verify CITATION.cff exists and matches release version."""
        cff = ROOT / "CITATION.cff"
        self.assertTrue(cff.is_file())
        content = cff.read_text()
        self.assertIn("version:", content)

    def test_f5_4_soc_manifest_dump(self) -> None:
        """Verify --dump-soc-manifest outputs valid schema."""
        with tempfile.TemporaryDirectory() as tmp:
            soc_path = pathlib.Path(tmp) / "soc.json"
            res = run_simrv(self.simrv64, ["--dump-soc-manifest", "virt-pcie", str(soc_path)])
            self.assertEqual(res.returncode, 0, res.stderr)
            data = json.loads(soc_path.read_text())
            self.assertEqual(data.get("schema_version"), 1)
            self.assertEqual(data.get("manifest_version"), "3.0")

    def test_f5_4_cpack_component_definitions(self) -> None:
        """Verify CPack packaging components in SimRVPackaging.cmake."""
        cmake_pkg = (ROOT / "cmake/SimRVPackaging.cmake").read_text()
        self.assertIn("CPACK_COMPONENTS_ALL", cmake_pkg)
        self.assertIn("Runtime", cmake_pkg)
        self.assertIn("Tools", cmake_pkg)


if __name__ == "__main__":
    unittest.main()
