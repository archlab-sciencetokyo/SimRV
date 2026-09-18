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
import shutil
import subprocess
import sys

BENCHMARKS = {
    'arithmetic_loop': '''
int main(void) {
    int a = 1;
    int b = 2;
    for (int i = 0; i < 100; i++) {
        a = (a * 3) + b;
        b = a ^ (b << 1);
    }
    return a + b;
}
''',
    'raw_hazard_chain': '''
int main(void) {
    register int v = 42;
    for (int i = 0; i < 100; i++) {
        asm volatile (
            "add %[v], %[v], %[i]\\n"
            "sub %[v], %[v], %[i]\\n"
            "add %[v], %[v], %[i]\\n"
            "sub %[v], %[v], %[i]\\n"
            : [v] "+r" (v) : [i] "r" (i)
        );
    }
    return v;
}
''',
    'branch_dense': '''
int main(void) {
    int s = 0;
    for (int i = 0; i < 100; i++) {
        if (i % 2 == 0) {
            s += i;
        } else {
            s -= i;
        }
    }
    return s;
}
''',
    'mul_div_suite': '''
int main(void) {
    int a = 12345;
    int b = 67;
    int s = 0;
    for (int i = 1; i <= 20; i++) {
        s += (a * i) / (b + i);
    }
    return s;
}
''',
    'load_store_array': '''
int arr[64];
int main(void) {
    for (int i = 0; i < 64; i++) {
        arr[i] = i * 3 + 1;
    }
    int sum = 0;
    for (int i = 0; i < 64; i++) {
        sum += arr[i];
    }
    return sum;
}
'''
}

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


def run_cmd(cmd, cwd=None):
    env = os.environ.copy()
    extra_paths = [
        "/var/archlab-modules/verilator/5.046/bin",
        "/var/archlab-modules/riscv-gnu-toolchain/2026.08.27/bin",
    ]
    env["PATH"] = ":".join([p for p in extra_paths if os.path.exists(p)] + [env.get("PATH", "")])
    if os.path.exists("/var/archlab-modules/gcc/16.2.0/lib64"):
        env["LD_LIBRARY_PATH"] = "/var/archlab-modules/gcc/16.2.0/lib64:" + env.get("LD_LIBRARY_PATH", "")
    res = subprocess.run(cmd, shell=True, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
    if res.returncode != 0:
        raise RuntimeError(f"Command failed ({res.returncode}): {cmd}\nOutput:\n{res.stdout}")
    return res.stdout


def get_toolchain():
    for gcc in ["riscv32-linux-gnu-gcc", "riscv32-unknown-elf-gcc", "riscv64-linux-gnu-gcc", "riscv64-unknown-elf-gcc"]:
        if shutil.which(gcc):
            prefix = gcc[:-3]
            return gcc, f"{prefix}objcopy", f"{prefix}objdump"
    raise RuntimeError("No suitable RISC-V toolchain found in PATH")


def extract_number(pattern, text):
    m = re.search(pattern, text)
    return int(m.group(1).replace(',', '')) if m else None


def main():
    parser = argparse.ArgumentParser(description="Evaluate RVComp RTL vs SimRV Cycle Parity")
    parser.add_argument('--out', type=str, default=None, help='Output path for JSON results')
    parser.add_argument('--cpu-profile', type=str, default="rvcomp", help='CPU profile name (default: rvcomp)')
    parser.add_argument('--cpu-config', type=str, default=None, help='Path to custom .cfg model file')
    parser.add_argument('--simrv-bin', type=str, default=None, help='Explicit path to SimRV executable')
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parents[1]
    rvcomp_dir = Path("/home/ren/workspace/lab/tools/RVComp")
    if not rvcomp_dir.exists():
        rvcomp_dir = repo_root.parent / "RVComp"

    if args.simrv_bin:
        simrv_bin = Path(args.simrv_bin)
    else:
        simrv_bin = repo_root / "build/rv32-release/SimRV"
        if not simrv_bin.exists():
            simrv_bin = repo_root / "build/rv64-release/SimRV"
    if not simrv_bin.exists():
        raise RuntimeError(f"SimRV binary not found under build/rv32-release or build/rv64-release")

    rvcom_bin = rvcomp_dir / "obj_dir/rvcom"
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
        rvcom_cmd = f"{rvcom_bin} +mem_file={hex128_path.resolve()} +max_cycles=10000000"
        rtl_out = run_cmd(rvcom_cmd, cwd=rvcomp_dir)

        rtl_minstret = extract_number(r'===> minstret\s*:\s*(\d+)', rtl_out)
        rtl_mcycle = extract_number(r'===> mcycle\s*:\s*(\d+)', rtl_out)
        rtl_br_hit = extract_number(r'===> branch hit\s*:\s*(\d+)', rtl_out)
        rtl_br_miss = extract_number(r'===> branch miss\s*:\s*(\d+)', rtl_out)
        rtl_br_penalty = extract_number(r'===> branch miss penalty total\s*:\s*(\d+)', rtl_out)
        rtl_ifu_stall = extract_number(r'===> ifu stall\s*:\s*(\d+)', rtl_out)
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
        simrv_out = run_cmd(simrv_cmd, cwd=repo_root)

        simrv_cycles = extract_number(r'Elapsed cycles \(clocks\)\s*:\s*[\d\.]*K?\s*\(([0-9,]+)\)', simrv_out)
        simrv_insts = extract_number(r'Executed instructions\s*:\s*[\d\.]*K?\s*\(([0-9,]+)\)', simrv_out)
        simrv_raw_stalls = extract_number(r'-\s*Data RAW Stalls\s*:\s*([0-9,]+)', simrv_out)
        simrv_ctrl_bubbles = extract_number(r'-\s*Control Bubbles\s*:\s*(\d+)', simrv_out)

        # In crt0.S, SimRV halts upon the 32-bit sw tohost (8000000c).
        # Verilator continues for 3 more instructions: auipc, sw tohost+4, unimp (80000018).
        # We report both raw and aligned parity:
        inst_delta = rtl_minstret - simrv_insts if (rtl_minstret and simrv_insts) else None

        # Compare core execution cycles (RTL core estimate vs SimRV elapsed cycles)
        cycle_delta = abs(simrv_cycles - rtl_core_est_cycle) if (simrv_cycles and rtl_core_est_cycle) else None
        cycle_err_pct = round(cycle_delta / rtl_core_est_cycle * 100, 2) if (cycle_delta is not None and rtl_core_est_cycle) else 0.0

        print(f"  RTL Verilator : {rtl_minstret} insts, {rtl_core_est_cycle} core cycles (mcycle: {rtl_mcycle}, branch miss: {rtl_br_miss}, ifu_stall: {rtl_ifu_stall}, lsu_stall: {rtl_lsu_stall}, mul_stall: {rtl_mul_stall}, div_stall: {rtl_div_stall})")
        print(f"  SimRV (rvcomp): {simrv_insts} insts, {simrv_cycles} cycles (raw stalls: {simrv_raw_stalls}, ctrl bubbles: {simrv_ctrl_bubbles})")
        print(f"  Alignment     : inst delta = {inst_delta} (HTIF shutdown handshake: 3), cycle delta = {cycle_delta} ({cycle_err_pct}% error)")

        results[name] = {
            'rtl_verilator': {
                'minstret': rtl_minstret,
                'mcycle': rtl_mcycle,
                'core_estimate_cycle': rtl_core_est_cycle,
                'branch_hit': rtl_br_hit,
                'branch_miss': rtl_br_miss,
                'branch_miss_penalty': rtl_br_penalty,
                'ifu_stall': rtl_ifu_stall,
                'lsu_stall': rtl_lsu_stall,
                'mul_stall': rtl_mul_stall,
                'div_stall': rtl_div_stall
            },
            'simrv': {
                'executed_instructions': simrv_insts,
                'elapsed_cycles': simrv_cycles,
                'data_raw_stalls': simrv_raw_stalls,
                'control_bubbles': simrv_ctrl_bubbles
            },
            'comparison': {
                'instruction_delta': inst_delta,
                'cycle_delta': cycle_delta,
                'cycle_error_percent': cycle_err_pct,
                'parity_confirmed': (inst_delta == 3 and cycle_err_pct < 5.0)
            }
        }

    out_path = Path(args.out) if args.out else repo_root / "build/rvcomp_parity.json"
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text(json.dumps(results, indent=2))
    print(f"\n[+] Saved parity evaluation results to {out_path}")


if __name__ == '__main__':
    main()
