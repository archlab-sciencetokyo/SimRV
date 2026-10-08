#!/usr/bin/env python3
"""Regression tests for release metadata, evidence, and archives."""

import importlib.util
import io
import json
import pathlib
import subprocess
import sys
import tarfile
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def load(name: str, path: pathlib.Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


release_check = load("release_check", ROOT / "scripts/release_check.py")


class ReleaseToolTests(unittest.TestCase):
    def test_checked_in_metadata(self):
        manifest = json.loads((ROOT / "release/release-manifest.json").read_text())
        release_check.verify_metadata(manifest)

    def test_evidence_rejects_unavailable_required_suite(self):
        manifest = json.loads((ROOT / "release/release-manifest.json").read_text())
        evidence = {"schema_version": 1, "simrv": {"version": manifest["version"]},
                    "summary": {"status": "failed", "passed": 1, "failed": 0, "skipped": 0, "unavailable": 1}}
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "evidence.json"
            path.write_text(json.dumps(evidence))
            with self.assertRaises(SystemExit):
                release_check.verify_evidence(path, manifest)

    def test_archive_rejects_parent_path(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "unsafe.tar.gz"
            with tarfile.open(path, "w:gz") as archive:
                info = tarfile.TarInfo("../SimRV")
                data = b"bad"
                info.size = len(data)
                archive.addfile(info, io.BytesIO(data))
            with self.assertRaises(SystemExit):
                release_check.verify_archive(path, "2.0.0")

    def test_corrupt_archive_is_rejected_cleanly(self):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "corrupt.tar.gz"
            path.write_bytes(b"not an archive")
            with self.assertRaises(SystemExit):
                release_check.verify_archive(path, "2.0.0")

    def test_checksum_mismatch_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            (root / "asset").write_bytes(b"data")
            checksum = root / "asset.sha256"
            checksum.write_text("0" * 64 + "  asset\n")
            with self.assertRaises(SystemExit):
                release_check.verify_checksum(checksum)

    def test_compare_is_evidence_only_by_default(self):
        def report(speed):
            return {"xlen": 64, "test_name": "demo", "simrv": {"stats": {"wall_speed": {"median": speed}}}}
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            baseline, candidate = root / "base.json", root / "candidate.json"
            baseline.write_text(json.dumps(report(100.0)))
            candidate.write_text(json.dumps(report(50.0)))
            result = subprocess.run([sys.executable, str(ROOT / "scripts/compare_benchmarks.py"),
                                     str(baseline), str(candidate)], check=False)
            self.assertEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
