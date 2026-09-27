#!/usr/bin/env python3
"""Run the manifest-driven SimRV paper-artifact workflow."""

import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys

from experiment_metadata import repository_revision

ROOT = pathlib.Path(__file__).resolve().parents[1]
EXPERIMENT_PATH = ROOT / "repro/experiment-manifest.json"
EXPERIMENT = json.loads(EXPERIMENT_PATH.read_text(encoding="utf-8"))


def run(command: list[str]) -> None:
    print("+", " ".join(map(str, command)), flush=True)
    ccache_temp = ROOT / "build/repro/.ccache-tmp"
    ccache_temp.mkdir(parents=True, exist_ok=True)
    environment = {**os.environ, "CCACHE_TEMPDIR": str(ccache_temp)}
    subprocess.run(command, cwd=ROOT, env=environment, check=True)


def result_path(output: pathlib.Path, key: str) -> pathlib.Path:
    return output / EXPERIMENT["outputs"][key]


def benchmark_command(configuration: dict, workload: dict, simrv: pathlib.Path,
                      spike: str, riscv_tests: pathlib.Path, output: pathlib.Path) -> list[str]:
    return [sys.executable, "scripts/benchmark.py", "--simrv", str(simrv),
            "--spike", spike, "--test", workload["target"],
            "--runs", str(EXPERIMENT["repetitions"]), "--warmups", str(EXPERIMENT["warmups"]),
            "--timeout", str(EXPERIMENT["timeout_seconds"]),
            "--limit", str(workload["instruction_limit"]),
            "--isa", configuration["isa"], "--vlen", str(configuration["vlen"]),
            "--execution-mode", configuration["execution_mode"],
            *[f"--simrv-arg={arg}" for arg in configuration.get("simrv_args", [])],
            "--riscv-tests-dir", str(riscv_tests), "--json", str(output)]


def configure_command(compiler: str, configuration: dict, build_dir: pathlib.Path,
                      args: argparse.Namespace) -> list[str]:
    cc, cxx = (("gcc", "g++") if compiler == "gcc" else ("clang", "clang++"))
    command = ["cmake", "-S", ".", "-B", str(build_dir), "-G", "Ninja",
               "-DCMAKE_BUILD_TYPE=Release", f"-DSIMRV_XLEN={configuration['xlen']}",
               f"-DCMAKE_C_COMPILER={cc}", f"-DCMAKE_CXX_COMPILER={cxx}",
               "-DSIMRV_WARNINGS_AS_ERRORS=ON", "-DSIMRV_ENABLE_SDL=OFF"]
    if args.riscv_tests_dir:
        command.append(f"-DRISCV_TESTS_DIR={args.riscv_tests_dir}")
    if args.vector_tests_dir and compiler == "gcc":
        command.append(f"-DSIMRV_VECTOR_TESTS_DIR={args.vector_tests_dir}")
    images = args.linux_images_root / f"rv{configuration['xlen']}"
    command.append(f"-DSIMRV_LINUX_IMAGES_DIR={images}")
    return command


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mode", choices=("quick", "full"), default="quick")
    parser.add_argument("--output", type=pathlib.Path, default=ROOT / "repro/results")
    parser.add_argument("--compiler", choices=("gcc", "clang", "all"), default="all")
    parser.add_argument("--riscv-tests-dir", type=pathlib.Path)
    parser.add_argument("--vector-tests-dir", type=pathlib.Path)
    parser.add_argument("--spike", default="spike")
    parser.add_argument("--linux-images-root", type=pathlib.Path, default=ROOT / "linux-images")
    parser.add_argument("--baseline", type=pathlib.Path,
                        help="Prior aggregate JSON used for regression comparison")
    parser.add_argument("--archive", type=pathlib.Path,
                        help="Output path for the deterministic submission archive")
    parser.add_argument("--no-package", action="store_true",
                        help="Run full evidence without creating the final archive")
    args = parser.parse_args()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    if args.mode == "full" and args.compiler != "all":
        parser.error("full artifact generation requires --compiler all")

    # 1. Validate the declared artifact contract and release metadata.
    run([sys.executable, "scripts/validate_schemas.py"])
    run([sys.executable, "scripts/release_check.py"])

    configurations = (EXPERIMENT["configurations"] if args.mode == "full"
                      else [EXPERIMENT["configurations"][-1]])
    compilers = (("gcc", "clang") if args.compiler == "all" and args.mode == "full"
                 else (("gcc",) if args.compiler == "all" else (args.compiler,)))
    reports: list[pathlib.Path] = []
    builds: dict[tuple[str, str], pathlib.Path] = {}

    # 2. Build and run the declared correctness matrix.
    for compiler in compilers:
        for configuration in configurations:
            arch = configuration["xlen"]
            build_dir = ROOT / "build/repro" / f"{compiler}-rv{arch}"
            shutil.rmtree(build_dir, ignore_errors=True)
            run(configure_command(compiler, configuration, build_dir, args))
            run(["cmake", "--build", str(build_dir)])
            builds[(compiler, configuration["id"])] = build_dir / "SimRV"
            evidence_path = args.output / f"evidence-{compiler}-rv{arch}.json"
            evidence = [sys.executable, "scripts/release_evidence.py", "--build-dir", str(build_dir),
                        "--arch", str(arch), "--compiler", compiler, "--output", str(evidence_path)]
            suites = ["native"] if args.mode == "quick" else ["native", "isa"]
            if args.mode == "full" and compiler == "gcc":
                suites.extend(["vector", "linux-pty", "package"])
            for suite in suites:
                evidence.extend(["--suite", suite])
            if args.riscv_tests_dir:
                evidence.extend(["--dependency", f"riscv_tests={args.riscv_tests_dir}"])
            if args.vector_tests_dir:
                evidence.extend(["--dependency", f"vector_tests={args.vector_tests_dir}"])
            run(evidence)
            reports.append(evidence_path)

    raw_reports: list[pathlib.Path] = []
    if args.mode == "full":
        # 3. Measure exactly the configurations and workloads declared by the manifest.
        if not args.riscv_tests_dir:
            raise SystemExit("--riscv-tests-dir is required for full performance evidence")
        performance_ids = set(EXPERIMENT["performance"]["configurations"])
        selected = [item for item in configurations if item["id"] in performance_ids]
        if {item["id"] for item in selected} != performance_ids:
            raise SystemExit("performance configuration IDs are not present in configurations")
        for configuration in selected:
            simrv = builds.get(("gcc", configuration["id"]))
            if not simrv:
                raise SystemExit(f"performance requires a GCC build for {configuration['id']}")
            for workload in EXPERIMENT["performance"]["workloads"]:
                raw = result_path(args.output, "raw") / configuration["id"] / f"{workload['id']}.json"
                run(benchmark_command(configuration, workload, simrv, args.spike,
                                      args.riscv_tests_dir.resolve(), raw))
                raw_reports.append(raw)

        # 4. Produce deterministic statistical summaries, tables, and plots.
        performance = EXPERIMENT["performance"]
        aggregate = result_path(args.output, "aggregate")
        run([sys.executable, "scripts/aggregate_experiments.py", *map(str, raw_reports),
             "--json", str(aggregate), "--table", str(result_path(args.output, "table")),
             "--plot", str(result_path(args.output, "plot")),
             "--bootstrap-resamples", str(performance["bootstrap_resamples"]),
             "--confidence", str(performance["confidence"])])
        aggregate_data = json.loads(aggregate.read_text(encoding="utf-8"))
        unstable = [{"workload": row["workload"], "xlen": row["xlen"],
                     "cv_percent": row["cv_percent"]}
                    for row in aggregate_data["results"]
                    if row["cv_percent"] > performance["maximum_cv_percent"]]
        if unstable and performance["policy"] == "enforced":
            raise SystemExit(f"benchmark CV threshold exceeded: {unstable}")

        # 5. Record the optional frozen-baseline comparison under the manifest policy.
        if args.baseline:
            comparison = [sys.executable, "scripts/compare_benchmarks.py", str(args.baseline.resolve()),
                          str(aggregate), "--maximum-regression",
                          str(performance["maximum_regression_percent"]), "--minimum-geomean",
                          str(performance["minimum_geomean_percent"]), "--json",
                          str(result_path(args.output, "comparison"))]
            if performance["policy"] == "enforced":
                comparison.append("--enforce")
            run(comparison)

        for suite, flags in (("asan-ubsan", ["-DSIMRV_ENABLE_ASAN=ON", "-DSIMRV_ENABLE_UBSAN=ON"]),
                             ("tsan", ["-DSIMRV_ENABLE_TSAN=ON"])):
            build_dir = ROOT / "build/repro" / suite
            shutil.rmtree(build_dir, ignore_errors=True)
            run(["cmake", "-S", ".", "-B", str(build_dir), "-G", "Ninja",
                 "-DCMAKE_BUILD_TYPE=Debug", "-DSIMRV_XLEN=64", "-DSIMRV_ENABLE_SDL=OFF",
                 "-DSIMRV_WARNINGS_AS_ERRORS=ON", *flags])
            run(["cmake", "--build", str(build_dir)])
            evidence_path = args.output / f"evidence-gcc-rv64-{suite}.json"
            run([sys.executable, "scripts/release_evidence.py", "--build-dir", str(build_dir),
                 "--arch", "64", "--compiler", "gcc", "--suite", suite,
                 "--output", str(evidence_path)])
            reports.append(evidence_path)

    # 6. Merge, verify, index, and package the self-contained submission evidence.
    merged = result_path(args.output, "evidence")
    run([sys.executable, "scripts/merge_evidence.py", *map(str, reports), "--output", str(merged)])
    if args.mode == "full":
        run([sys.executable, "scripts/release_check.py", "--evidence", str(merged)])
    archive = args.archive.resolve() if args.archive else ROOT / "SimRV-paper-artifact.tar.gz"
    index = {"schema_version": 1, "mode": args.mode, "manifest": str(EXPERIMENT_PATH.relative_to(ROOT)),
             "repository_revision": repository_revision(ROOT),
             "raw_benchmarks": [str(path.relative_to(args.output)) for path in raw_reports],
             "evidence": [str(path.relative_to(args.output)) for path in reports],
             "merged_evidence": str(merged.relative_to(args.output)),
             "aggregate": EXPERIMENT["outputs"]["aggregate"] if args.mode == "full" else None,
             "unstable_benchmarks": unstable if args.mode == "full" else [],
             "comparison": (EXPERIMENT["outputs"]["comparison"] if args.baseline else None),
             "archive": str(archive)}
    index_path = result_path(args.output, "index")
    index_path.write_text(json.dumps(index, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if args.mode == "full" and not args.no_package:
        run([sys.executable, "scripts/package_repro.py", "--results", str(args.output),
             "--output", str(archive)])
        run([sys.executable, "scripts/release_check.py", "--checksum",
             str(archive.with_suffix(archive.suffix + ".sha256"))])
    print(f"reproduction complete: {index_path}")


if __name__ == "__main__":
    main()
