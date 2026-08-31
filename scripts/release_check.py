#!/usr/bin/env python3
"""Validate SimRV release metadata and optionally packaged binaries."""

import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import sys
import tarfile
import tempfile


ROOT = pathlib.Path(__file__).resolve().parents[1]
MANIFEST_PATH = ROOT / "release" / "release-manifest.json"
SCHEMA_DIR = ROOT / "release" / "schemas"


def fail(message: str) -> None:
    print(f"release-check: {message}", file=sys.stderr)
    raise SystemExit(1)


def source_version() -> str:
    match = re.search(
        r'^set\(SIMRV_VERSION "([^"]+)"\)$',
        (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"),
        re.MULTILINE,
    )
    if not match:
        fail("cannot read SIMRV_VERSION from CMakeLists.txt")
    return match.group(1)


def verify_metadata(manifest: dict) -> None:
    if manifest.get("schema_version") != 2:
        fail("release manifest schema_version must be 2")
    for schema in ("release-manifest.schema.json", "evidence.schema.json", "experiment.schema.json"):
        if not (SCHEMA_DIR / schema).is_file():
            fail(f"missing schema: release/schemas/{schema}")
    version = source_version()
    expected_tag = f"v{version}"
    if manifest.get("version") != version:
        fail(f"manifest version {manifest.get('version')!r} != source version {version!r}")
    if manifest.get("release_tag") != expected_tag:
        fail(f"manifest tag must be {expected_tag}")
    changelog = (ROOT / "CHANGELOG.md").read_text(encoding="utf-8")
    if f"## [{expected_tag}]" not in changelog:
        fail(f"CHANGELOG.md has no {expected_tag} heading")
    for artifact in manifest.get("artifacts", []):
        if version not in artifact:
            fail(f"artifact name does not contain version: {artifact}")
    dependencies = manifest.get("dependencies", {})
    for name, dependency in dependencies.items():
        for field in ("url", "revision", "license", "redistribute"):
            if field not in dependency:
                fail(f"dependency {name!r} is missing {field!r}")
    suites = manifest.get("required_suites", [])
    if not suites or len({suite.get("id") for suite in suites}) != len(suites):
        fail("required suites must have unique non-empty IDs")
    if any(suite.get("allow_skip") is not False for suite in suites):
        fail("required release suites must not permit skips")
    performance = manifest.get("performance", {})
    if performance.get("policy") != "evidence-only" or performance.get("blocking") is not False:
        fail("performance policy must be non-blocking evidence-only")


def verify_evidence(path: pathlib.Path, manifest: dict) -> None:
    evidence = json.loads(path.read_text(encoding="utf-8"))
    if evidence.get("schema_version") != 1:
        fail(f"unsupported evidence schema in {path}")
    if evidence.get("simrv", {}).get("version") != manifest["version"]:
        fail(f"evidence version does not match {manifest['version']}")
    summary = evidence.get("summary", {})
    required = {"status", "passed", "failed", "skipped", "unavailable"}
    if not required <= summary.keys():
        fail(f"evidence summary is incomplete: {path}")
    if summary["skipped"] or summary["unavailable"] or summary["failed"]:
        fail(f"release evidence contains non-passing required suites: {path}")
    if summary["status"] != "passed":
        fail(f"release evidence status is not passed: {path}")
    observed = set()
    for configuration in evidence.get("configurations", []):
        xlen = configuration.get("xlen")
        compiler = configuration.get("compiler")
        observed.update((xlen, compiler, suite.get("id")) for suite in configuration.get("suites", []))
    expected = {(arch, compiler, suite["id"])
                for suite in manifest["required_suites"]
                for arch in suite["arches"] for compiler in suite["compilers"]}
    missing = sorted(expected - observed)
    if missing:
        fail(f"release evidence does not cover required matrix entries: {missing}")


def verify_binary(binary: pathlib.Path, version: str) -> None:
    if not binary.is_file():
        fail(f"binary not found: {binary}")
    result = subprocess.run(
        [binary, "--version"], text=True, capture_output=True, timeout=10, check=False
    )
    if result.returncode != 0 or version not in result.stdout + result.stderr:
        fail(f"{binary} does not report version {version}")
    help_result = subprocess.run(
        [binary, "--help"], text=True, capture_output=True, timeout=10, check=False
    )
    if help_result.returncode != 0 or "Usage:" not in help_result.stdout:
        fail(f"{binary} failed the CLI smoke test")


def verify_archive(archive: pathlib.Path, version: str) -> None:
    if not archive.is_file():
        fail(f"archive not found: {archive}")
    try:
        with tempfile.TemporaryDirectory(prefix="simrv-release-") as temp_dir:
            with tarfile.open(archive, "r:gz") as tar:
                members = tar.getmembers()
                if any(pathlib.PurePosixPath(m.name).is_absolute() or ".." in pathlib.PurePosixPath(m.name).parts for m in members):
                    fail(f"unsafe path in archive: {archive}")
                tar.extractall(temp_dir, filter="data")
            verify_binary(pathlib.Path(temp_dir) / "SimRV", version)
    except (tarfile.TarError, OSError) as error:
        fail(f"cannot validate archive {archive}: {error}")


def verify_checksum(checksum_file: pathlib.Path) -> None:
    try:
        lines = checksum_file.read_text(encoding="utf-8").splitlines()
        if not lines:
            fail(f"empty checksum file: {checksum_file}")
        for line in lines:
            digest, filename = line.split(maxsplit=1)
            if not re.fullmatch(r"[0-9a-fA-F]{64}", digest):
                fail(f"invalid checksum digest: {digest}")
            target = checksum_file.parent / filename.lstrip("*")
            actual = hashlib.sha256(target.read_bytes()).hexdigest()
            if actual != digest.lower():
                fail(f"checksum mismatch: {target}")
    except (OSError, ValueError) as error:
        fail(f"cannot validate checksum file {checksum_file}: {error}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", action="append", type=pathlib.Path, default=[])
    parser.add_argument("--archive", action="append", type=pathlib.Path, default=[])
    parser.add_argument("--checksum", action="append", type=pathlib.Path, default=[])
    parser.add_argument("--evidence", action="append", type=pathlib.Path, default=[])
    args = parser.parse_args()

    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    verify_metadata(manifest)
    for binary in args.binary:
        verify_binary(binary.resolve(), manifest["version"])
    for archive in args.archive:
        verify_archive(archive.resolve(), manifest["version"])
    for checksum in args.checksum:
        verify_checksum(checksum.resolve())
    for evidence in args.evidence:
        verify_evidence(evidence.resolve(), manifest)
    print(f"release-check: {manifest['release_tag']} metadata and artifacts are valid")


if __name__ == "__main__":
    main()
