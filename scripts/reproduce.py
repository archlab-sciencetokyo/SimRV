#!/usr/bin/env python3
"""Single entry point for SimRV correctness and experiment reproduction."""

import argparse
import json
import pathlib
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
EXPERIMENT = json.loads((ROOT / "repro/experiment-manifest.json").read_text())


def run(command: list[str]) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=ROOT, check=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mode", choices=("quick", "full"), default="quick")
    parser.add_argument("--output", type=pathlib.Path, default=ROOT / "repro/results")
    parser.add_argument("--compiler", choices=("gcc", "clang", "all"), default="all")
    parser.add_argument("--riscv-tests-dir", type=pathlib.Path)
    parser.add_argument("--vector-tests-dir", type=pathlib.Path)
    parser.add_argument("--spike", default="spike")
    parser.add_argument("--linux-images-root", type=pathlib.Path, default=ROOT / "linux-images")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    run([sys.executable, "scripts/release_check.py"])

    configurations = EXPERIMENT["configurations"] if args.mode == "full" else [EXPERIMENT["configurations"][-1]]
    compilers = ("gcc", "clang") if args.compiler == "all" and args.mode == "full" else (("gcc",) if args.compiler == "all" else (args.compiler,))
    reports = []
    for compiler in compilers:
        for configuration in configurations:
            arch = configuration["xlen"]
            build_dir = ROOT / "build/repro" / f"{compiler}-rv{arch}"
            cc, cxx = (("gcc", "g++") if compiler == "gcc" else ("clang", "clang++"))
            configure = ["cmake", "-S", ".", "-B", str(build_dir), "-G", "Ninja",
                         "-DCMAKE_BUILD_TYPE=Release", f"-DSIMRV_XLEN={arch}",
                         f"-DCMAKE_C_COMPILER={cc}", f"-DCMAKE_CXX_COMPILER={cxx}",
                         "-DSIMRV_WARNINGS_AS_ERRORS=ON"]
            if args.riscv_tests_dir:
                configure.append(f"-DRISCV_TESTS_DIR={args.riscv_tests_dir.resolve()}")
            if args.vector_tests_dir and compiler == "gcc":
                configure.append(f"-DSIMRV_VECTOR_TESTS_DIR={args.vector_tests_dir.resolve()}")
            images = args.linux_images_root / f"rv{arch}"
            configure.append(f"-DSIMRV_LINUX_IMAGES_DIR={images.resolve()}")
            run(configure)
            run(["cmake", "--build", str(build_dir)])
            output = args.output / f"evidence-{compiler}-rv{arch}.json"
            evidence = [sys.executable, "scripts/release_evidence.py", "--build-dir", str(build_dir),
                        "--arch", str(arch), "--compiler", compiler, "--output", str(output)]
            suites = ["native"] if args.mode == "quick" else ["native", "isa"]
            if args.mode == "full" and compiler == "gcc":
                suites.extend(["vector", "linux-pty", "package"])
            for suite in suites:
                evidence.extend(["--suite", suite])
            if args.riscv_tests_dir:
                evidence.extend(["--dependency", f"riscv_tests={args.riscv_tests_dir.resolve()}"])
            if args.vector_tests_dir:
                evidence.extend(["--dependency", f"vector_tests={args.vector_tests_dir.resolve()}"])
            run(evidence)
            reports.append(output)

            if args.mode == "full" and compiler == "gcc" and arch == 64:
                if not args.riscv_tests_dir:
                    raise SystemExit("--riscv-tests-dir is required for full performance evidence")
                raw = args.output / "raw" / f"benchmark-rv{arch}.json"
                run([sys.executable, "scripts/benchmark.py", "--simrv", str(build_dir / "SimRV"),
                     "--spike", args.spike, "--suite", "realworld",
                     "--runs", str(EXPERIMENT["repetitions"]), "--warmups", str(EXPERIMENT["warmups"]),
                     "--timeout", str(EXPERIMENT["timeout_seconds"]),
                     "--riscv-tests-dir", str(args.riscv_tests_dir.resolve()), "--json", str(raw)])

    if args.mode == "full":
        sanitizer_builds = [
            ("asan-ubsan", ["-DSIMRV_ENABLE_ASAN=ON", "-DSIMRV_ENABLE_UBSAN=ON"]),
            ("tsan", ["-DSIMRV_ENABLE_TSAN=ON"]),
        ]
        for suite, flags in sanitizer_builds:
            build_dir = ROOT / "build/repro" / suite
            run(["cmake", "-S", ".", "-B", str(build_dir), "-G", "Ninja",
                 "-DCMAKE_BUILD_TYPE=Debug", "-DSIMRV_XLEN=64",
                 "-DSIMRV_WARNINGS_AS_ERRORS=ON", *flags])
            run(["cmake", "--build", str(build_dir)])
            output = args.output / f"evidence-gcc-rv64-{suite}.json"
            run([sys.executable, "scripts/release_evidence.py", "--build-dir", str(build_dir),
                 "--arch", "64", "--compiler", "gcc", "--suite", suite, "--output", str(output)])
            reports.append(output)

        aggregate = EXPERIMENT["outputs"]
        raw_reports = sorted((args.output / "raw").glob("*.json"))
        run([sys.executable, "scripts/aggregate_experiments.py", *map(str, raw_reports),
             "--json", str(args.output / "aggregate.json"), "--table", str(args.output / "table.md"),
             "--plot", str(args.output / "throughput.svg")])

    merged = args.output / "evidence.json"
    run([sys.executable, "scripts/merge_evidence.py", *map(str, reports), "--output", str(merged)])
    if args.mode == "full":
        run([sys.executable, "scripts/release_check.py", "--evidence", str(merged)])

    index = {"schema_version": 1, "mode": args.mode,
             "manifest": "repro/experiment-manifest.json", "evidence": [str(path) for path in reports],
             "merged_evidence": str(merged)}
    (args.output / "index.json").write_text(json.dumps(index, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"reproduction complete: {args.output / 'index.json'}")


if __name__ == "__main__":
    main()
