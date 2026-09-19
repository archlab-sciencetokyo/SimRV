#!/usr/bin/env python3
"""Unified entry point for SimRV experiment preparation, execution, and packaging."""

import argparse
import gzip
import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tarfile
import time


ROOT = pathlib.Path(__file__).resolve().parents[1]
EXPERIMENT = json.loads((ROOT / "repro/experiment-manifest.json").read_text())
MANIFEST = json.loads((ROOT / "release/release-manifest.json").read_text())
STATIC_MEMBERS = [
    "CITATION.cff", "LICENSE", "README.md", "SECURITY.md", "docs/evaluation/release.md",
    "docs/architecture/compliance.md", "repro/README.md", "repro/experiment-manifest.json"
]


def run(command: list[str], cwd: pathlib.Path | None = None, env: dict[str, str] | None = None) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, cwd=cwd or ROOT, env=env, check=True)


def git_revision(path: pathlib.Path) -> str:
    return subprocess.run(
        ["git", "-C", str(path), "rev-parse", "HEAD"],
        text=True, capture_output=True, check=True
    ).stdout.strip()


def prepare_dependencies(directory: pathlib.Path, check_only: bool = False, build_vector: bool = False) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    for name in ("riscv_tests", "vector_tests", "spike"):
        dependency = MANIFEST["dependencies"][name]
        target = directory / name.replace("_", "-")
        if not target.exists():
            if check_only:
                raise SystemExit(f"missing dependency: {target}")
            run(["git", "clone", "--filter=blob:none", dependency["url"], str(target)])
        if not check_only:
            run(["git", "fetch", "--tags", "origin"], target)
            run(["git", "checkout", "--detach", dependency["revision"]], target)
            run(["git", "submodule", "update", "--init", "--recursive"], target)
        observed = git_revision(target)
        expected = dependency["revision"]
        if len(expected) >= 40 and observed != expected:
            raise SystemExit(f"{name}: expected {expected}, found {observed}")
        print(f"{name}: {observed}")

    vector_dir = directory / "vector-tests"
    if build_vector and not check_only:
        if not shutil.which("go"):
            raise SystemExit("Go is required to build vector tests")
        run(["make", "-j1"], vector_dir)

    print("Pinned source dependencies are ready. Toolchains and Linux images are built/downloaded separately.")


def merge_evidence(report_paths: list[pathlib.Path], output: pathlib.Path) -> None:
    reports = [json.loads(p.read_text(encoding="utf-8")) for p in sorted(report_paths)]
    if not reports:
        raise SystemExit("at least one evidence report is required to merge")
    versions = {r["simrv"]["version"] for r in reports}
    revisions = {r["simrv"]["revision"] for r in reports}
    if len(versions) != 1 or len(revisions) != 1:
        raise SystemExit("evidence reports describe different SimRV versions or revisions")

    configurations = []
    dependencies = {}
    for r in reports:
        configurations.extend(r["configurations"])
        for name, value in r.get("dependencies", {}).items():
            dependencies.setdefault(name, {}).update(value)

    configurations.sort(key=lambda c: (c.get("xlen", 0), c.get("compiler", ""),
                                       ",".join(s.get("id", "") for s in c.get("suites", []))))
    suites = [s for c in configurations for s in c.get("suites", [])]
    summary = {k: sum(s.get(k, 0) for s in suites) for k in ("passed", "failed", "skipped")}
    summary["unavailable"] = sum(s.get("status") == "unavailable" for s in suites)
    summary["status"] = "passed" if not summary["failed"] and not summary["skipped"] and not summary["unavailable"] else "failed"

    merged = {
        "$schema": "release/schemas/evidence.schema.json",
        "schema_version": 1,
        "simrv": reports[0]["simrv"],
        "host": {"system": "multiple CI workers", "machine": "x86_64"},
        "dependencies": dependencies,
        "configurations": configurations,
        "summary": summary
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(merged, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"merged evidence written: {output}")


def package_repro(results_dir: pathlib.Path, output: pathlib.Path | None = None) -> pathlib.Path:
    tar_output = output or (ROOT / f"SimRV-repro-v{MANIFEST['version']}.tar.gz")
    members = [(ROOT / path, pathlib.Path(path)) for path in STATIC_MEMBERS]
    members += [(path, path.relative_to(ROOT)) for path in sorted((ROOT / "release/schemas").glob("*.json"))]
    members += [(path, path.relative_to(ROOT)) for path in sorted((ROOT / "scripts").glob("*.py"))]
    if results_dir.is_dir():
        members += [(p, pathlib.Path("results") / p.relative_to(results_dir))
                    for p in sorted(results_dir.rglob("*")) if p.is_file()]
    missing = [str(p) for p, _ in members if not p.is_file()]
    if missing:
        raise SystemExit(f"missing reproduction inputs: {missing}")

    tar_output.parent.mkdir(parents=True, exist_ok=True)
    with tar_output.open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode="w") as archive:
                for src, arcname in sorted(set(members), key=lambda item: str(item[1])):
                    info = archive.gettarinfo(str(src), arcname=str(arcname))
                    info.uid = info.gid = 0
                    info.uname = info.gname = ""
                    info.mtime = 0
                    with src.open("rb") as s:
                        archive.addfile(info, s)

    digest = hashlib.sha256(tar_output.read_bytes()).hexdigest()
    tar_output.with_suffix(tar_output.suffix + ".sha256").write_text(f"{digest}  {tar_output.name}\n", encoding="utf-8")
    print(f"package created: {tar_output}")
    return tar_output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--prepare", action="store_true", help="Download and prepare pinned dependencies")
    parser.add_argument("--check-deps", action="store_true", help="Verify pinned dependencies exist")
    parser.add_argument("--build-vector-tests", action="store_true", help="Build vector tests during preparation")
    parser.add_argument("--deps-dir", type=pathlib.Path, default=ROOT / ".cache/repro")

    parser.add_argument("--package", action="store_true", help="Package reproduction results into tarball")
    parser.add_argument("--package-output", type=pathlib.Path, help="Output tarball path for packaging")

    parser.add_argument("--mode", choices=("quick", "full"), default="quick", help="Reproduction run mode")
    parser.add_argument("--output", type=pathlib.Path, default=ROOT / "repro/results")
    parser.add_argument("--compiler", choices=("gcc", "clang", "all"), default="all")
    parser.add_argument("--riscv-tests-dir", type=pathlib.Path)
    parser.add_argument("--vector-tests-dir", type=pathlib.Path)
    parser.add_argument("--spike", default="spike")
    parser.add_argument("--linux-images-root", type=pathlib.Path, default=ROOT / "linux-images")

    args = parser.parse_args()

    if args.prepare or args.check_deps:
        prepare_dependencies(args.deps_dir, check_only=args.check_deps, build_vector=args.build_vector_tests)
        return

    if args.package:
        package_repro(args.output, output=args.package_output)
        return

    # Normal reproduction execution
    args.output.mkdir(parents=True, exist_ok=True)
    run([sys.executable, "scripts/release_check.py"])

    generator = "Ninja" if shutil.which("ninja") else "Unix Makefiles"
    configurations = EXPERIMENT["configurations"] if args.mode == "full" else [EXPERIMENT["configurations"][-1]]
    compilers = ("gcc", "clang") if args.compiler == "all" and args.mode == "full" else (("gcc",) if args.compiler == "all" else (args.compiler,))
    reports = []

    for compiler in compilers:
        for configuration in configurations:
            arch = configuration["xlen"]
            build_dir = ROOT / "build/repro" / f"{compiler}-rv{arch}"
            cc, cxx = (("gcc", "g++") if compiler == "gcc" else ("clang", "clang++"))
            configure = ["cmake", "-S", ".", "-B", str(build_dir), "-G", generator,
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
            run(["cmake", "-S", ".", "-B", str(build_dir), "-G", generator,
                 "-DCMAKE_BUILD_TYPE=Debug", "-DSIMRV_XLEN=64",
                 "-DSIMRV_WARNINGS_AS_ERRORS=ON", *flags])
            run(["cmake", "--build", str(build_dir)])
            output = args.output / f"evidence-gcc-rv64-{suite}.json"
            run([sys.executable, "scripts/release_evidence.py", "--build-dir", str(build_dir),
                 "--arch", "64", "--compiler", "gcc", "--suite", suite, "--output", str(output)])
            reports.append(output)

        raw_reports = sorted((args.output / "raw").glob("*.json"))
        run([sys.executable, "scripts/benchmark.py", "aggregate", *map(str, raw_reports),
             "--json", str(args.output / "aggregate.json"), "--table", str(args.output / "table.md"),
             "--plot", str(args.output / "throughput.svg")])

    merged = args.output / "evidence.json"
    merge_evidence(reports, merged)
    if args.mode == "full":
        run([sys.executable, "scripts/release_check.py", "--evidence", str(merged)])

    index = {"schema_version": 1, "mode": args.mode,
             "manifest": "repro/experiment-manifest.json", "evidence": [str(path) for path in reports],
             "merged_evidence": str(merged)}
    (args.output / "index.json").write_text(json.dumps(index, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"reproduction complete: {args.output / 'index.json'}")


if __name__ == "__main__":
    main()
