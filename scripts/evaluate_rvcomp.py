#!/usr/bin/env python3
"""
Evaluate cycle-accuracy and behavioral parity between RVComp (synthesizable
SystemVerilog RTL simulated with Verilator) and SimRV's cycle-accurate 5-stage
pipeline model using --cpu-profile rvcomp.
"""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess

from rtl_parity.common import BENCHMARKS, compare_retirement_traces, extract_number, get_toolchain

CRT0_SRC = '''/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Archlab, Science Tokyo
 */
    .equ UART_BASE, 0x10000000
    .equ DRAM_BASE, 0x80000000
    .equ DRAM_SIZE, 0x10000000 # 256 MiB
    .section .text.init
    .align 4
    .globl _start
_start:
    j reset_vector

trap_vector: # exit(1)
    li t0, 1
write_tohost:
    sw t0, tohost, t1
    sw zero, tohost+4, t1
    unimp
    j write_tohost

    .globl uart_putc
uart_putc:
    li t0, UART_BASE
    sb a0, 0(t0)
    ret

reset_vector:
    li x1, 0
    li x3, 0
    li x4, 0
    li x5, 0
    li x6, 0
    li x7, 0
    li x8, 0
    li x9, 0
    li x10, 0
    li x11, 0
    li x12, 0
    li x13, 0
    li x14, 0
    li x15, 0
    li x16, 0
    li x17, 0
    li x18, 0
    li x19, 0
    li x20, 0
    li x21, 0
    li x22, 0
    li x23, 0
    li x24, 0
    li x25, 0
    li x26, 0
    li x27, 0
    li x28, 0
    li x29, 0
    li x30, 0
    li x31, 0
    li sp, DRAM_BASE+DRAM_SIZE # stack pointer
    la t0, trap_vector
    csrw mtvec, t0
    jal main
    j trap_vector

    .pushsection .tohost,"aw",@progbits
    .align 6
    .global tohost
tohost:
    .dword 0
    .size tohost, 8

    .align 6
    .global fromhost
fromhost:
    .dword 0
    .size fromhost, 8
    .popsection
'''

LINKER_SCRIPT = '''/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2025 Archlab, Science Tokyo
 */
OUTPUT_ARCH("riscv")
ENTRY("_start")

SECTIONS
{
    . = 0x80000000;
    .text.init : { *(.text.init) }
    .text.startup : { *(.text.startup) }
    .text : { *(.text) }
    . = 0x80001000;
    .tohost : { *(.tohost) }
    .data : { *(.data) }
    .rodata : { *(.rodata) }
    .bss : { *(.bss) *(COMMON) }
    /DISCARD/ : { *(.eh_frame) *(.eh_frame_hdr) *(.note.gnu.build-id) *(.comment) }
    _end = .;
}
'''


def run_cmd(cmd, cwd=None, extra_env=None):
    env = os.environ.copy()
    if extra_env:
        env.update(extra_env)
    res = subprocess.run(cmd, shell=True, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
    if res.returncode != 0:
        raise RuntimeError(f"Command failed ({res.returncode}): {cmd}\nOutput:\n{res.stdout}")
    return res.stdout


def analyze_rtl_ifu_trace(rtl_out):
    pattern = re.compile(
        r'^IFUTRACE cycle=(\d+) pc=([0-9a-fA-F]+).*'
        r'arvalid=(\d+).*rvalid=(\d+)',
        re.MULTILINE,
    )
    outstanding = []
    completed = []
    for match in pattern.finditer(rtl_out):
        cycle, pc, arvalid, rvalid = match.groups()
        cycle = int(cycle)
        if int(arvalid):
            outstanding.append((cycle, int(pc, 16)))
        if int(rvalid) and outstanding:
            request_cycle, request_pc = outstanding.pop(0)
            completed.append({
                'pc': f'0x{request_pc:08x}',
                'request_cycle': request_cycle,
                'response_cycle': cycle,
                'response_latency': cycle - request_cycle,
            })

    latencies = [event['response_latency'] for event in completed]
    return {
        'requests': len(completed) + len(outstanding),
        'completed_requests': len(completed),
        'incomplete_requests': len(outstanding),
        'response_latency_min': min(latencies) if latencies else None,
        'response_latency_max': max(latencies) if latencies else None,
        'response_latency_mean': round(sum(latencies) / len(latencies), 2) if latencies else None,
        'first_completed_requests': completed[:4],
    }


def main(arguments=None):
    parser = argparse.ArgumentParser(description="Evaluate RVComp RTL vs SimRV Cycle Parity")
    parser.add_argument('--out', type=str, default=None, help='Output path for JSON results')
    parser.add_argument('--cpu-profile', type=str, default="rvcomp", help='CPU profile name (default: rvcomp)')
    parser.add_argument('--cpu-config', type=str, default=None, help='Path to custom .cfg model file')
    parser.add_argument('--simrv-bin', type=str, default=None, help='Explicit path to SimRV executable')
    parser.add_argument('--rvcomp-dir', type=str, default=os.environ.get('RVCOMP_DIR'),
                        help='RVComp checkout (default: $RVCOMP_DIR or sibling ../RVComp)')
    parser.add_argument('--rvcomp-bin', type=str, default=None,
                        help='Explicit RVComp Verilator executable')
    parser.add_argument('--trace-dir', type=str, default=None,
                        help='Write RTL/SimRV retirement traces and report first divergence')
    args = parser.parse_args(arguments)

    repo_root = Path(__file__).resolve().parents[1]
    rvcomp_dir = Path(args.rvcomp_dir).expanduser() if args.rvcomp_dir else repo_root.parent / "RVComp"

    if args.simrv_bin:
        simrv_bin = Path(args.simrv_bin)
    else:
        simrv_bin = repo_root / "build/rv32-release/SimRV"
        if not simrv_bin.exists():
            simrv_bin = repo_root / "build/rv64-release/SimRV"
    if not simrv_bin.exists():
        raise RuntimeError(f"SimRV binary not found under build/rv32-release or build/rv64-release")

    rvcom_bin = Path(args.rvcomp_bin).expanduser() if args.rvcomp_bin else rvcomp_dir / "obj_dir/rvcom"
    if not rvcom_bin.exists():
        raise RuntimeError(f"RVComp Verilator simulator binary not found at {rvcom_bin}")

    work_dir = repo_root / "build/rvcomp_eval"
    work_dir.mkdir(parents=True, exist_ok=True)
    build_sub = work_dir / "build"
    build_sub.mkdir(parents=True, exist_ok=True)

    (work_dir / "crt0.S").write_text(CRT0_SRC)
    (work_dir / "link.ld").write_text(LINKER_SCRIPT)

    gcc, objcopy, objdump = get_toolchain()
    print(f"[*] Toolchain: {gcc}, {objcopy}, {objdump}")
    print(f"[*] SimRV:     {simrv_bin}")
    print(f"[*] RVComp:    {rvcom_bin}")

    results = {}

    for name, src in BENCHMARKS.items():
        print(f"\n--- Benchmark: {name} ---")
        c_path = work_dir / f"{name}.c"
        c_path.write_text(src)

        elf_path = build_sub / f"{name}.elf"
        bin_path = build_sub / f"{name}.bin"
        hex64_path = build_sub / f"{name}.64.hex"
        hex128_path = build_sub / f"{name}.128.hex"

        # 1. Compile ELF
        compile_cmd = (
            f"{gcc} -march=rv32im_zicsr -mabi=ilp32 -O2 -nostdlib -ffreestanding "
            f"-T{work_dir / 'link.ld'} -o {elf_path} {work_dir / 'crt0.S'} {c_path}"
        )
        run_cmd(compile_cmd, cwd=work_dir)

        # 2. Generate 128-bit hex for Verilator top.sv
        run_cmd(f"{objcopy} -O binary {elf_path} {bin_path}.tmp", cwd=work_dir)
        run_cmd(f"dd if={bin_path}.tmp of={bin_path} conv=sync bs=16KiB", cwd=work_dir)
        run_cmd(f"od -v -An -tx8 -w8 {bin_path} | sed 's/^ *\\([0-9a-f]\\+\\)/\\1/' > {hex64_path}", cwd=work_dir)
        run_cmd(f"awk '{{if(NR%2){{buf=$0}}else{{print $0 buf; buf=\"\"}}}}' {hex64_path} > {hex128_path}", cwd=work_dir)

        # 3. Run RVComp RTL Verilator
        parity_trace = " +parity_trace" if args.trace_dir else ""
        rvcom_cmd = f"{rvcom_bin} +mem_file={hex128_path.resolve()} +max_cycles=10000000{parity_trace}"
        rtl_out = run_cmd(rvcom_cmd, cwd=rvcomp_dir)

        rtl_minstret = extract_number(r'===> minstret\s*:\s*(\d+)', rtl_out)
        rtl_mcycle = extract_number(r'===> mcycle\s*:\s*(\d+)', rtl_out)
        rtl_tohost_mcycle = extract_number(r'TOHOST_LOW mcycle=(\d+)', rtl_out)
        rtl_tohost_minstret = extract_number(r'TOHOST_LOW mcycle=\d+ minstret=(\d+)', rtl_out)
        rtl_br_hit = extract_number(r'===> branch hit\s*:\s*(\d+)', rtl_out)
        rtl_br_miss = extract_number(r'===> branch miss\s*:\s*(\d+)', rtl_out)
        rtl_br_penalty = extract_number(r'===> branch miss penalty total\s*:\s*(\d+)', rtl_out)
        rtl_ifu_stall = extract_number(r'===> ifu stall\s*:\s*(\d+)', rtl_out)
        rtl_l0_miss = extract_number(r'===> L0 icache miss\s*:\s*(\d+)', rtl_out)
        rtl_l1i_miss = extract_number(r'===> L1 icache miss\s*:\s*(\d+)', rtl_out)
        rtl_l2_miss = extract_number(r'===> L2 cache miss\s*:\s*(\d+)', rtl_out)
        rtl_lsu_stall = extract_number(r'===> lsu stall\s*:\s*(\d+)', rtl_out)
        rtl_mul_stall = extract_number(r'===> mul stall\s*:\s*(\d+)', rtl_out)
        rtl_div_stall = extract_number(r'===> div stall\s*:\s*(\d+)', rtl_out)
        rtl_core_est_cycle = extract_number(r'===> total estimate cycle\s*:\s*(\d+)', rtl_out)

        # 4. Run SimRV in cycle-accurate mode
        if args.cpu_config:
            model_arg = f"--cpu-config {Path(args.cpu_config).resolve()}"
        else:
            model_arg = f"--cpu-profile {args.cpu_profile}"
        simrv_cmd = f"{simrv_bin} --cli --ca -m {elf_path.resolve()} -H 0x80000000 {model_arg}"
        simrv_out = run_cmd(
            simrv_cmd,
            cwd=repo_root,
            extra_env={'SIMRV_RETIRE_TRACE': '1', 'SIMRV_CACHE_TRACE': '1'}
            if args.trace_dir else None,
        )

        simrv_cycles = extract_number(r'Elapsed cycles \(clocks\)\s*:\s*[\d\.]*K?\s*\(([0-9,]+)\)', simrv_out)
        simrv_insts = extract_number(r'Executed instructions\s*:\s*[\d\.]*K?\s*\(([0-9,]+)\)', simrv_out)
        simrv_raw_stalls = extract_number(r'-\s*Data RAW Stalls\s*:\s*([0-9,]+)', simrv_out)
        simrv_ctrl_bubbles = extract_number(r'-\s*Control Bubbles\s*:\s*(\d+)', simrv_out)
        simrv_l0_fills = len(re.findall(r'^CACHETRACE .*hit=false', simrv_out, re.MULTILINE))
        simrv_backing_fills = len(
            re.findall(r'^CACHETRACE .*backing_miss=true', simrv_out, re.MULTILINE)
        )
        rtl_ifu_trace = analyze_rtl_ifu_trace(rtl_out)

        # SimRV stops while executing the low-word HTIF store. The instrumented RTL
        # testbench reports its counters at that same store; its retirement counter
        # includes the store itself while SimRV's does not.
        rtl_reference_cycles = rtl_tohost_mcycle or rtl_mcycle
        rtl_reference_insts = rtl_tohost_minstret or rtl_minstret
        inst_delta = rtl_reference_insts - simrv_insts if (rtl_reference_insts and simrv_insts) else None

        measured_cycle_delta = simrv_cycles - rtl_reference_cycles if (simrv_cycles and rtl_reference_cycles) else None
        measured_cycle_err_pct = round(abs(measured_cycle_delta) / rtl_reference_cycles * 100, 2) if (measured_cycle_delta is not None and rtl_reference_cycles) else 0.0
        estimate_cycle_delta = simrv_cycles - rtl_core_est_cycle if (simrv_cycles and rtl_core_est_cycle) else None
        estimate_cycle_err_pct = round(abs(estimate_cycle_delta) / rtl_core_est_cycle * 100, 2) if (estimate_cycle_delta is not None and rtl_core_est_cycle) else 0.0

        print(f"  RTL Verilator : {rtl_minstret} insts, {rtl_core_est_cycle} core cycles (mcycle: {rtl_mcycle}, branch miss: {rtl_br_miss}, ifu_stall: {rtl_ifu_stall}, lsu_stall: {rtl_lsu_stall}, mul_stall: {rtl_mul_stall}, div_stall: {rtl_div_stall})")
        print(f"  SimRV (rvcomp): {simrv_insts} insts, {simrv_cycles} cycles (raw stalls: {simrv_raw_stalls}, ctrl bubbles: {simrv_ctrl_bubbles})")
        estimate_offset = rtl_core_est_cycle - rtl_mcycle
        print(f"  HTIF checkpoint: RTL {rtl_reference_insts} insts / {rtl_reference_cycles} cycles; inst delta = {inst_delta} (store retirement: 1), cycle delta = {measured_cycle_delta:+d} ({measured_cycle_err_pct}% error)")
        print(f"  Diagnostics   : estimate delta = {estimate_cycle_delta:+d} ({estimate_cycle_err_pct}% error), final RTL estimate offset = {estimate_offset}")

        trace_comparison = None
        if args.trace_dir:
            trace_dir = Path(args.trace_dir)
            trace_dir.mkdir(parents=True, exist_ok=True)
            (trace_dir / f'{name}.rtl.log').write_text(rtl_out)
            (trace_dir / f'{name}.simrv.log').write_text(simrv_out)
            trace_comparison = compare_retirement_traces(rtl_out, simrv_out)
            trace_comparison['rtl_ifu'] = rtl_ifu_trace
            trace_comparison['simrv_l0_fills'] = simrv_l0_fills
            trace_comparison['simrv_backing_fills'] = simrv_backing_fills
            print(f"  Trace          : {trace_comparison}")

        results[name] = {
            'rtl_verilator': {
                'minstret': rtl_minstret,
                'mcycle': rtl_mcycle,
                'tohost_mcycle': rtl_tohost_mcycle,
                'tohost_minstret': rtl_tohost_minstret,
                'core_estimate_cycle': rtl_core_est_cycle,
                'branch_hit': rtl_br_hit,
                'branch_miss': rtl_br_miss,
                'branch_miss_penalty': rtl_br_penalty,
                'ifu_stall': rtl_ifu_stall,
                'l0_icache_miss': rtl_l0_miss,
                'l1_icache_miss': rtl_l1i_miss,
                'l2_cache_miss': rtl_l2_miss,
                'lsu_stall': rtl_lsu_stall,
                'mul_stall': rtl_mul_stall,
                'div_stall': rtl_div_stall
            },
            'simrv': {
                'executed_instructions': simrv_insts,
                'elapsed_cycles': simrv_cycles,
                'data_raw_stalls': simrv_raw_stalls,
                'control_bubbles': simrv_ctrl_bubbles,
                'l0_timing_fills': simrv_l0_fills,
                'backing_fills': simrv_backing_fills,
            },
            'comparison': {
                'instruction_delta': inst_delta,
                'measured_cycle_delta': measured_cycle_delta,
                'measured_cycle_error_percent': measured_cycle_err_pct,
                'estimate_cycle_delta': estimate_cycle_delta,
                'estimate_cycle_error_percent': estimate_cycle_err_pct,
                'rtl_estimate_offset': estimate_offset,
                'instruction_parity_at_tohost': (inst_delta == 1),
                'exact_cycle_parity': (measured_cycle_delta == 0),
                'parity_confirmed': (inst_delta == 1 and measured_cycle_delta == 0),
                'retirement_trace': trace_comparison,
            }
        }

    out_path = Path(args.out) if args.out else repo_root / "build/rvcomp_parity.json"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(results, indent=2))
    print(f"\n[+] Saved parity evaluation results to {out_path}")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
