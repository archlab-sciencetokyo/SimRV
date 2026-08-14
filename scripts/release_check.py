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
    with tempfile.TemporaryDirectory(prefix="simrv-release-") as temp_dir:
        with tarfile.open(archive, "r:gz") as tar:
            members = tar.getmembers()
            if any(pathlib.PurePosixPath(m.name).is_absolute() or ".." in pathlib.PurePosixPath(m.name).parts for m in members):
                fail(f"unsafe path in archive: {archive}")
            tar.extractall(temp_dir, filter="data")
        verify_binary(pathlib.Path(temp_dir) / "SimRV", version)


def verify_checksum(checksum_file: pathlib.Path) -> None:
    for line in checksum_file.read_text(encoding="utf-8").splitlines():
        digest, filename = line.split(maxsplit=1)
        target = checksum_file.parent / filename.lstrip("*")
        actual = hashlib.sha256(target.read_bytes()).hexdigest()
        if actual != digest:
            fail(f"checksum mismatch: {target}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", action="append", type=pathlib.Path, default=[])
    parser.add_argument("--archive", action="append", type=pathlib.Path, default=[])
    parser.add_argument("--checksum", action="append", type=pathlib.Path, default=[])
    args = parser.parse_args()

    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    verify_metadata(manifest)
    for binary in args.binary:
        verify_binary(binary.resolve(), manifest["version"])
    for archive in args.archive:
        verify_archive(archive.resolve(), manifest["version"])
    for checksum in args.checksum:
        verify_checksum(checksum.resolve())
    print(f"release-check: {manifest['release_tag']} metadata and artifacts are valid")


if __name__ == "__main__":
    main()
