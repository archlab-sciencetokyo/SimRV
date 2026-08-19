#!/usr/bin/env python3
import os
import sys
import glob
import argparse
import subprocess
import re
from concurrent.futures import ThreadPoolExecutor, as_completed
import multiprocessing

def parse_args():
    parser = argparse.ArgumentParser(description="Generate, compile, and run vector tests for SimRV.")
    parser.add_argument("--simrv", required=True, help="Path to SimRV binary")
    parser.add_argument("--xlen", type=int, choices=[32, 64], required=True, help="XLEN (32 or 64)")
    parser.add_argument("--gcc", required=True, help="Path to RISC-V GCC cross-compiler")
    parser.add_argument("--objcopy", required=True, help="Path to objcopy binary")
    parser.add_argument("--nm", required=True, help="Path to nm binary")
    parser.add_argument("--work-dir", required=True, help="Path to directory for generated/compiled artifacts")
    parser.add_argument("--vector-tests-dir", required=True, help="Checked-out chipsalliance/riscv-vector-tests directory")
    parser.add_argument("--vlen", type=int, default=256, help="Vector register length used by generated tests")
    parser.add_argument("--jobs", type=int, default=multiprocessing.cpu_count(), help="Number of parallel jobs to run")
    return parser.parse_args()

def run_cmd(cmd, shell=False):
    res = subprocess.run(cmd, shell=shell, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    return res

def get_tohost_addr(nm_bin, elf_path):
    default_tohost = "0x80006000"
    if not os.path.exists(nm_bin):
        return default_tohost
    res = run_cmd([nm_bin, "-g", elf_path])
    if res.returncode == 0:
        # Match format: e.g. "0000000080006000 D tohost"
        m = re.search(r"([0-9a-fA-F]+)\s+[DdTtGgBb]\s+tohost", res.stdout)
        if m:
            return "0x" + m.group(1).lstrip("0")
    return default_tohost

def compile_and_run_test(test_ctx):
    s_file = test_ctx["s_file"]
    test_name = test_ctx["test_name"]
    gcc = test_ctx["gcc"]
    objcopy = test_ctx["objcopy"]
    nm = test_ctx["nm"]
    simrv = test_ctx["simrv"]
    work_dir = test_ctx["work_dir"]
    xlen = test_ctx["xlen"]

    elf_file = os.path.join(work_dir, f"{test_name}.elf")
    bin_file = os.path.join(work_dir, f"{test_name}.bin")

    # Arch and ABI configuration
    if xlen == 64:
        vec_arch = "rv64gcv_zfh_zvfh_zvbb_zvbc"
        vec_abi = "lp64d"
    else:
        vec_arch = "rv32gcv_zfh_zvfh_zvbb_zvbc"
        vec_abi = "ilp32d"

    # 1. Compile
    compile_cmd = [
        gcc,
        f"-march={vec_arch}",
        f"-mabi={vec_abi}",
        "-static",
        "-mcmodel=medany",
        "-fvisibility=hidden",
        "-nostdlib",
        "-nostartfiles",
        "-I", os.path.join(test_ctx["vector_tests_dir"], "env", "riscv-test-env"),
        "-I", os.path.join(test_ctx["vector_tests_dir"], "env", "riscv-test-env", "p"),
        "-I", os.path.join(test_ctx["vector_tests_dir"], "env"),
        "-I", os.path.join(test_ctx["vector_tests_dir"], "macros", "general"),
        "-T", os.path.join(test_ctx["vector_tests_dir"], "env", "riscv-test-env", "p", "link.ld"),
        s_file,
        "-o", elf_file
    ]

    res = run_cmd(compile_cmd)
    if res.returncode != 0:
        return {"name": test_name, "status": "COMPILE_FAIL", "error": res.stderr}

    # 2. Objcopy
    objcopy_cmd = [objcopy, "-O", "binary", elf_file, bin_file]
    res = run_cmd(objcopy_cmd)
    if res.returncode != 0:
        return {"name": test_name, "status": "OBJCOPY_FAIL", "error": res.stderr}

    # 3. Find tohost
    tohost_addr = get_tohost_addr(nm, elf_file)

    # 4. Run SimRV
    sim_cmd = [
        simrv,
        "--cli",
        "-m", bin_file,
        "-e", "2000000",
        "-b",
        "-H", tohost_addr
    ]
    
    try:
        res = subprocess.run(sim_cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=10)
    except subprocess.TimeoutExpired:
        return {"name": test_name, "status": "TIMEOUT", "error": "Execution timed out (10s)"}

    if "ISA TEST PASS" in res.stdout:
        return {"name": test_name, "status": "PASS", "error": ""}
    elif "ISA TEST FAIL" in res.stdout:
        return {"name": test_name, "status": "FAIL", "error": res.stdout + res.stderr}
    else:
        return {"name": test_name, "status": "EXEC_FAIL", "error": f"Exit code: {res.returncode}\nStdout: {res.stdout}\nStderr: {res.stderr}"}

def main():
    args = parse_args()

    vector_tests_dir = os.path.abspath(args.vector_tests_dir)
    generator_path = os.path.join(vector_tests_dir, "bin", "riscv-vector-tests-generator")
    configs_path = os.path.join(vector_tests_dir, "configs")

    if not os.path.exists(generator_path):
        print(f"Error: Vector test generator not found at '{generator_path}'")
        sys.exit(1)

    os.makedirs(args.work_dir, exist_ok=True)

    # 1. Run Generator
    print("Generating vector assembly files...", flush=True)
    gen_cmd = [
        generator_path,
        "-XLEN", str(args.xlen),
        "-VLEN", str(args.vlen),
        "-configs", configs_path,
        "-stage1output", args.work_dir,
        "-march", "gcv_zvbb_zvbc"
    ]
    res = run_cmd(gen_cmd)
    if res.returncode != 0:
        print(f"Failed to generate vector tests: {res.stderr}")
        sys.exit(1)

    # Load supported operation IDs from OperationId.hpp
    supported_ops = set()
    op_header_path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "include", "simrv", "isa", "OperationId.hpp")
    if os.path.exists(op_header_path):
        with open(op_header_path, "r") as f:
            for line in f:
                # Find words starting with V followed by uppercase letters, numbers, or underscores
                for m in re.finditer(r"\b(V[A-Z0-9_]+)\b", line):
                    supported_ops.add(m.group(1))
    else:
        print(f"Warning: OperationId.hpp not found at {op_header_path}", flush=True)

    s_files = glob.glob(os.path.join(args.work_dir, "*.S"))
    
    # Filter out segment load/stores, fault-only-first tests, unsupported instructions, and float e16 tests (since Zfh is unsupported)
    filtered_s_files = []
    for f in s_files:
        basename = os.path.basename(f)
        if "seg" in basename or "ff" in basename:
            continue
        # Check for float e16 tests (Zfh is unsupported)
        if basename.startswith("vf"):
            try:
                with open(f, "r") as s_file:
                    content = s_file.read()
                    if "SEW: e16" in content:
                        continue
            except Exception:
                pass
        # Extract instruction name before the first '-'
        m = re.match(r"^([a-z0-9_]+)-", basename)
        if m:
            insn_name = m.group(1).upper()
            if insn_name in supported_ops:
                filtered_s_files.append(f)
        else:
            filtered_s_files.append(f)
    s_files = filtered_s_files

    if not s_files:
        print("No generated assembly (.S) files found!")
        sys.exit(1)

    print(f"Found {len(s_files)} generated vector tests. Compiling and running with {args.jobs} jobs...", flush=True)

    test_contexts = []
    for s_file in s_files:
        test_name = os.path.splitext(os.path.basename(s_file))[0]
        test_contexts.append({
            "s_file": s_file,
            "test_name": test_name,
            "gcc": args.gcc,
            "objcopy": args.objcopy,
            "nm": args.nm,
            "simrv": args.simrv,
            "work_dir": args.work_dir,
            "xlen": args.xlen,
            "vector_tests_dir": vector_tests_dir,
        })

    passed = 0
    failed = []
    total = len(test_contexts)

    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        futures = {executor.submit(compile_and_run_test, ctx): ctx for ctx in test_contexts}
        for i, future in enumerate(as_completed(futures), 1):
            result = future.result()
            if result["status"] == "PASS":
                passed += 1
            else:
                failed.append(result)
            
            if i % 100 == 0 or i == total:
                print(f"Progress: {i}/{total} tests completed ({passed} passed, {len(failed)} failed)...", flush=True)

    print("\n--- Vector Test Summary ---")
    print(f"Total: {total}")
    print(f"Passed: {passed}")
    print(f"Failed: {len(failed)}")

    if failed:
        print("\nFailed Tests Details:")
        for f in failed[:10]:  # Show first 10 failures
            print(f"- {f['name']} ({f['status']}):")
            print(f.get("error", "").strip())
            print("-" * 40)
        if len(failed) > 10:
            print(f"... and {len(failed) - 10} more failures.")
        sys.exit(1)
    else:
        print("All vector tests passed successfully!")
        sys.exit(0)

if __name__ == "__main__":
    main()
