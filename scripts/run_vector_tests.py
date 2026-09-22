#!/usr/bin/env python3
import os
import sys
import glob
import argparse
import subprocess
import re
from concurrent.futures import ThreadPoolExecutor, as_completed
import multiprocessing
import tempfile

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
    parser.add_argument("--jobs", type=int, default=min(4, multiprocessing.cpu_count()), help="Number of parallel jobs to run")
    parser.add_argument("--pattern", default=".*", help="Generator instruction-name regex")
    parser.add_argument("--generator", help="Prebuilt vector test generator")
    parser.add_argument("--configs-dir", help="Generator configs directory (default: configs/v)")
    return parser.parse_args()

def run_cmd(cmd, shell=False, cwd=None):
    res = subprocess.run(cmd, shell=shell, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
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
        vec_arch = "rv64gcv"
        vec_abi = "lp64d"
    else:
        vec_arch = "rv32gcv"
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
    args.work_dir = os.path.abspath(args.work_dir)
    os.makedirs(args.work_dir, exist_ok=True)
    run_dir = tempfile.mkdtemp(prefix="vector-run-", dir=args.work_dir)

    vector_tests_dir = os.path.abspath(args.vector_tests_dir)
    generator_path = args.generator or os.path.join(vector_tests_dir, "bin", "riscv-vector-tests-generator")
    if not os.path.exists(generator_path) and not args.generator:
        generator_path = os.path.join(os.path.abspath(args.work_dir), "riscv-vector-tests-generator")
    configs_path = os.path.abspath(args.configs_dir) if args.configs_dir else os.path.join(vector_tests_dir, "configs", "v")

    if not os.path.exists(generator_path):
        gen_src_dir = os.path.join(vector_tests_dir, "generator")
        if os.path.exists(gen_src_dir):
            gen_env = os.environ.copy()
            res = subprocess.run(["go", "build", "-o", generator_path, "."], cwd=vector_tests_dir, env=gen_env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            if res.returncode != 0:
                print(f"Failed to build vector test generator: {res.stderr}")

    if not os.path.exists(generator_path):
        print(f"Error: Vector test generator not found at '{generator_path}'")
        sys.exit(1)

    # 1. Run Generator
    print("Generating vector assembly files...", flush=True)
    gen_cmd = [
        generator_path,
        "-XLEN", str(args.xlen),
        "-VLEN", str(args.vlen),
        "-configs", configs_path,
        "-stage1output", run_dir,
        "-march", "gcv",
        "-pattern", args.pattern
    ]
    try:
        res = run_cmd(gen_cmd, cwd=run_dir)
    except Exception:
        res = None

    if res is None or res.returncode != 0:
        print(f"Failed to generate vector tests: {res.stderr if res else 'generator could not start'}")
        sys.exit(1)

    s_files = glob.glob(os.path.join(run_dir, "*.S"))

    if not s_files:
        print("No generated assembly (.S) files found!")
        sys.exit(1)

    print(f"Found {len(s_files)} generated vector tests. Compiling and running with {args.jobs} jobs...", flush=True)

    gcc_bin = args.gcc
    objcopy_bin = args.objcopy
    nm_bin = args.nm
    import shutil
    if not any(k in os.path.basename(gcc_bin) for k in ["riscv", "cross"]):
        for cand in ["riscv64-unknown-elf-gcc", "riscv32-unknown-elf-gcc", "riscv64-linux-gnu-gcc", "riscv32-linux-gnu-gcc", "riscv-none-elf-gcc", "/var/archlab-modules/riscv-gnu-toolchain/2026.03.13/bin/riscv64-unknown-elf-gcc"]:
            found = cand if os.path.isabs(cand) and os.path.exists(cand) else shutil.which(cand)
            if found:
                gcc_bin = found
                break
    if not any(k in os.path.basename(objcopy_bin) for k in ["riscv", "cross"]):
        for cand in ["riscv64-unknown-elf-objcopy", "riscv32-unknown-elf-objcopy", "riscv64-linux-gnu-objcopy", "riscv32-linux-gnu-objcopy", "riscv-none-elf-objcopy", "/var/archlab-modules/riscv-gnu-toolchain/2026.03.13/bin/riscv64-unknown-elf-objcopy"]:
            found = cand if os.path.isabs(cand) and os.path.exists(cand) else shutil.which(cand)
            if found:
                objcopy_bin = found
                break
    if not any(k in os.path.basename(nm_bin) for k in ["riscv", "cross"]):
        for cand in ["riscv64-unknown-elf-nm", "riscv32-unknown-elf-nm", "riscv64-linux-gnu-nm", "riscv32-linux-gnu-nm", "riscv-none-elf-nm", "/var/archlab-modules/riscv-gnu-toolchain/2026.03.13/bin/riscv64-unknown-elf-nm"]:
            found = cand if os.path.isabs(cand) and os.path.exists(cand) else shutil.which(cand)
            if found:
                nm_bin = found
                break

    test_contexts = []
    for s_file in s_files:
        test_name = os.path.splitext(os.path.basename(s_file))[0]
        test_contexts.append({
            "s_file": s_file,
            "test_name": test_name,
            "gcc": gcc_bin,
            "objcopy": objcopy_bin,
            "nm": nm_bin,
            "simrv": args.simrv,
            "work_dir": run_dir,
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
