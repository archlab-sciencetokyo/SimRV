"""CFU Proving Ground parity adapter."""

import argparse
import json
import os
import shutil
import subprocess
from pathlib import Path

from rtl_parity.common import BENCHMARKS, compare_retirement_traces, extract_number, get_toolchain


RTL_FILES = ("top.v", "main.v", "proc.v", "cfu.v", "config.vh")


def run_command(command, cwd=None, env=None):
    result = subprocess.run(command, cwd=cwd, env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, check=False)
    if result.returncode != 0:
        raise RuntimeError(
            f"Command failed ({result.returncode}): {' '.join(map(str, command))}\n"
            f"Output:\n{result.stdout}"
        )
    return result.stdout


def instrument_top(source):
    marker = "    wire sda, scl, dc, res;"
    instrumentation = r'''
    always @(posedge clk) begin
        if ($test$plusargs("parity_trace") && !m0.rst && !cpu_sim_fini &&
            !m0.cpu.stall && m0.cpu.MaWb_v && !m0.cpu.stall_i) begin
            $write("CYCLETRACE cycle=%0d inst=%0d pc=%08x ir=%08x\n",
                   mcycle, minstret, m0.cpu.MaWb_pc, m0.cpu.MaWb_ir);
        end
    end

'''
    if marker not in source:
        raise RuntimeError("CFU-PG top.v does not contain the expected instance marker")
    return source.replace(marker, instrumentation + marker, 1)


def binary_to_words(binary_path, capacity_bytes):
    data = binary_path.read_bytes()
    if len(data) > capacity_bytes:
        raise RuntimeError(f"{binary_path.name} is {len(data)} bytes; capacity is {capacity_bytes}")
    data += bytes((-len(data)) % 4)
    return [int.from_bytes(data[offset:offset + 4], "little")
            for offset in range(0, len(data), 4)]


def write_memory_include(path, memory_name, words):
    lines = ["initial begin"]
    lines.extend(f"    {memory_name}[{index}] = 32'h{word:08x};"
                 for index, word in enumerate(words))
    lines.append("end")
    path.write_text("\n".join(lines) + "\n")


def prepare_rtl(cfu_dir, build_dir, elf_path, objcopy, verilator):
    rtl_dir = build_dir / "rtl"
    rtl_dir.mkdir(parents=True, exist_ok=True)
    for filename in RTL_FILES:
        source = (cfu_dir / filename).read_text()
        if filename == "top.v":
            source = instrument_top(source)
        (rtl_dir / filename).write_text(source)

    imem_bin = build_dir / "imem.bin"
    dmem_bin = build_dir / "dmem.bin"
    run_command([objcopy, "-O", "binary", "--only-section=.text", str(elf_path), str(imem_bin)])
    run_command([objcopy, "-O", "binary", "--only-section=.data", "--only-section=.rodata",
                 "--only-section=.bss", str(elf_path), str(dmem_bin)])
    write_memory_include(rtl_dir / "memi.txt", "imem", binary_to_words(imem_bin, 32 * 1024))
    write_memory_include(rtl_dir / "memd.txt", "dmem", binary_to_words(dmem_bin, 16 * 1024))

    build_env = os.environ.copy()
    build_env["CCACHE_DISABLE"] = "1"
    run_command([verilator, "--binary", "--top-module", "top", "--Wno-WIDTHTRUNC",
                 "--Wno-WIDTHEXPAND", "-o", "top", "top.v", "main.v", "proc.v", "cfu.v"],
                cwd=rtl_dir, env=build_env)
    return rtl_dir / "obj_dir" / "top"


def create_parser():
    parser = argparse.ArgumentParser(description="Evaluate CFU-PG RTL against SimRV")
    parser.add_argument("--cfu-dir", default=None,
                        help="CFU-Proving-Ground checkout (default: sibling checkout)")
    parser.add_argument("--simrv-bin", default=None)
    parser.add_argument("--cpu-config", default=None)
    parser.add_argument("--trace-dir", default=None)
    parser.add_argument("--out", default=None)
    parser.add_argument("--verilator", default=None)
    return parser


def run(arguments):
    args = create_parser().parse_args(arguments)
    repo_root = Path(__file__).resolve().parents[3]
    cfu_dir = Path(args.cfu_dir).resolve() if args.cfu_dir else repo_root.parent / "CFU-Proving-Ground"
    simrv_bin = Path(args.simrv_bin).resolve() if args.simrv_bin else repo_root / "build/rv32-release/SimRV"
    cpu_config = (Path(args.cpu_config).resolve() if args.cpu_config
                  else repo_root / "configs/models/cfu-provingground.cfg")
    verilator = args.verilator or shutil.which("verilator")
    if not simrv_bin.exists():
        raise RuntimeError(f"SimRV binary not found: {simrv_bin}")
    if not verilator:
        raise RuntimeError("verilator was not found on PATH; pass --verilator")
    for filename in RTL_FILES:
        if not (cfu_dir / filename).exists():
            raise RuntimeError(f"CFU-PG RTL file not found: {cfu_dir / filename}")

    gcc, objcopy, _ = get_toolchain()
    work_root = repo_root / "build/rtl_parity/cfu-pg"
    work_root.mkdir(parents=True, exist_ok=True)
    crt0 = cfu_dir / "app/crt0.s"
    linker = cfu_dir / "app/link.ld"
    results = {}

    print("[*] Target:    CFU Proving Ground")
    print(f"[*] SimRV:     {simrv_bin}")
    print(f"[*] RTL:       {cfu_dir}")
    for name, source in BENCHMARKS.items():
        print(f"\n--- Benchmark: {name} ---")
        build_dir = work_root / name
        build_dir.mkdir(parents=True, exist_ok=True)
        source_path = build_dir / f"{name}.c"
        elf_path = build_dir / f"{name}.elf"
        source_path.write_text(source)
        run_command([gcc, "-march=rv32im", "-mabi=ilp32", "-Os", "-nostdlib",
                     "-ffreestanding", f"-T{linker}", "-o", str(elf_path), str(crt0),
                     str(source_path)])
        rtl_bin = prepare_rtl(cfu_dir, build_dir, elf_path, objcopy, verilator)
        rtl_out = run_command([str(rtl_bin), "+parity_trace"])
        simrv_out = run_command([str(simrv_bin), "--cli", "--ca", "-m", str(elf_path),
                                 "-H", "0x80000000", "--cpu-config", str(cpu_config)],
                                env={**os.environ, "SIMRV_RETIRE_TRACE": "1"})

        rtl_cycles = extract_number(r"===> mcycle\s*:\s*(\d+)", rtl_out)
        rtl_insts = extract_number(r"===> minstret\s*:\s*(\d+)", rtl_out)
        simrv_cycles = extract_number(
            r"Elapsed cycles \(clocks\)\s*:\s*[\d\.]*K?\s*\(([0-9,]+)\)", simrv_out)
        simrv_insts = extract_number(
            r"Executed instructions\s*:\s*[\d\.]*K?\s*\(([0-9,]+)\)", simrv_out)
        trace = compare_retirement_traces(rtl_out, simrv_out)
        cycle_delta = simrv_cycles - rtl_cycles
        instruction_delta = simrv_insts - rtl_insts
        architecture_match = trace["first_architectural_divergence"] is None
        print(f"  RTL           : {rtl_insts} insts, {rtl_cycles} cycles")
        print(f"  SimRV         : {simrv_insts} insts, {simrv_cycles} cycles")
        print(f"  Delta         : {instruction_delta:+d} insts, {cycle_delta:+d} cycles")
        results[name] = {
            "rtl": {"instructions": rtl_insts, "cycles": rtl_cycles},
            "simrv": {"instructions": simrv_insts, "cycles": simrv_cycles},
            "comparison": {
                "instruction_delta": instruction_delta,
                "cycle_delta": cycle_delta,
                "architectural_trace_match": architecture_match,
                "exact_cycle_parity": cycle_delta == 0,
                "retirement_trace": trace,
            },
        }
        if args.trace_dir:
            trace_dir = Path(args.trace_dir)
            trace_dir.mkdir(parents=True, exist_ok=True)
            (trace_dir / f"{name}.rtl.log").write_text(rtl_out)
            (trace_dir / f"{name}.simrv.log").write_text(simrv_out)

    output = Path(args.out) if args.out else repo_root / "build/cfu_pg_parity.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(results, indent=2) + "\n")
    print(f"\n[+] Saved CFU-PG parity results to {output}")
    return 0
