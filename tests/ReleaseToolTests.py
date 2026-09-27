#!/usr/bin/env python3
"""Regression tests for release metadata, evidence, archives, and aggregation."""

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
sys.path.insert(0, str(ROOT / "scripts"))


def load(name: str, path: pathlib.Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


release_check = load("release_check", ROOT / "scripts/release_check.py")
aggregate = load("aggregate_experiments", ROOT / "scripts/aggregate_experiments.py")
metadata = load("experiment_metadata", ROOT / "scripts/experiment_metadata.py")
benchmark = load("benchmark", ROOT / "scripts/benchmark.py")


class ReleaseToolTests(unittest.TestCase):
    def test_configuration_fingerprint_is_stable(self):
        first = {"xlen": 64, "isa": "rv64gc", "vlen": 256}
        second = {"vlen": 256, "isa": "rv64gc", "xlen": 64}
        first_json, first_id = metadata.configuration_fingerprint(first)
        second_json, second_id = metadata.configuration_fingerprint(second)
        self.assertEqual(first_json, second_json)
        self.assertEqual(first_id, second_id)
        self.assertEqual(len(first_id), 16)

    def test_version_probe_falls_back_to_help_output(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = pathlib.Path(directory) / "tool"
            executable.write_text(
                "#!/bin/sh\n"
                "if [ \"$1\" = --version ]; then exit 1; fi\n"
                "echo 'tool help provenance' >&2\n"
                "exit 1\n"
            )
            executable.chmod(0o755)
            self.assertEqual(metadata.command_version(str(executable)), "tool help provenance")

    def test_experiment_manifest_configuration_is_explicit(self):
        manifest = json.loads((ROOT / "repro/experiment-manifest.json").read_text())
        for configuration in manifest["configurations"]:
            self.assertIn(configuration["execution_mode"], ("high-performance", "cycle-accurate"))
            self.assertIsInstance(configuration["simrv_args"], list)

    def test_benchmark_commands_use_same_isa_and_limit(self):
        simrv = benchmark.simrv_benchmark_command(
            "SimRV", "guest.elf", "rv64gcbv", 256, "high-performance", 1234,
            "0x80001000", []
        )
        spike = benchmark.spike_benchmark_command("spike", "guest.elf", "rv64gcbv", 1234)
        self.assertEqual(simrv[simrv.index("--isa") + 1], "rv64gcbv")
        self.assertIn("--instructions=1234", spike)
        self.assertEqual(simrv[-2:], ["-e", "1234"])

    def test_benchmark_commands_support_guest_completion(self):
        simrv = benchmark.simrv_benchmark_command(
            "SimRV", "guest.elf", "rv64gc", None, "cycle-accurate", 0,
            "0x80001000", []
        )
        spike = benchmark.spike_benchmark_command("spike", "guest.elf", "rv64gc", 0)
        self.assertNotIn("-e", simrv)
        self.assertFalse(any(arg.startswith("--instructions=") for arg in spike))

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

    def test_aggregation_is_deterministic(self):
        report = {"suite_results": [{"xlen": 64, "test_name": "demo",
                  "simrv": {"runs_wall_speed_kips": [3.0, 1.0, 2.0]}}]}
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            source = root / "raw.json"
            source.write_text(json.dumps(report))
            first = aggregate.load_rows([source])
            second = aggregate.load_rows([source])
            self.assertEqual(first, second)
            self.assertEqual(first[0]["median_kips"], 2.0)

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
