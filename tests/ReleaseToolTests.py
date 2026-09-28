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
import types
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))


def load(name: str, path: pathlib.Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


release_check = load("release_check", ROOT / "scripts/release_check.py")
benchmark = load("benchmark", ROOT / "scripts/benchmark.py")
aggregate = benchmark
benchmark_modes = load("benchmark_modes", ROOT / "scripts/benchmark_modes.py")
metadata = load("experiment_metadata", ROOT / "scripts/experiment_metadata.py")


class ReleaseToolTests(unittest.TestCase):
    def test_release_workflow_publishes_curated_assets(self):
        workflow = (ROOT / ".github/workflows/release-binaries.yml").read_text()
        cmake = (ROOT / "CMakeLists.txt").read_text()
        manifest = json.loads((ROOT / "release/release-manifest.json").read_text())
        self.assertIn("publish-release:", workflow)
        self.assertIn("SimRV-rpm-packages-v${VERSION}.tar.gz", workflow)
        self.assertIn("SimRV-deb-packages-v${VERSION}.tar.gz", workflow)
        self.assertNotIn(".sha256", workflow)
        self.assertNotIn("reproducibility-framework:", workflow)
        self.assertNotIn("documentation:", workflow)
        self.assertNotIn("--contents \"$runtime_deb\" | grep -q", workflow)
        self.assertNotIn("-qpl \"$runtime_rpm\" | grep -q", workflow)
        self.assertIn("set(CPACK_COMPONENTS_ALL Runtime Tools Benchmark)", cmake)
        self.assertNotIn("CPACK_DEBIAN_DEVELOPMENT", cmake)
        self.assertNotIn("CPACK_RPM_DEVELOPMENT", cmake)
        self.assertNotIn("development_deb", workflow)
        self.assertIn('test "${#rpms[@]}" -eq 3', workflow)
        self.assertIn('test "${#debs[@]}" -eq 3', workflow)
        self.assertIn("prerelease: ${{ contains(github.ref_name, '-') }}", workflow)
        self.assertIn("dpkg-scanpackages . /dev/null > Packages", workflow)
        self.assertIn("deb-packages/rv32", workflow)
        self.assertIn("deb-packages/rv64", workflow)
        self.assertIn("grep -c '^Package: '", workflow)
        self.assertIn('createrepo_c "rpm-packages/${arch}"', workflow)
        self.assertIn("rpm-packages/rv32", workflow)
        self.assertIn("rpm-packages/rv64", workflow)
        self.assertIn("repodata/repomd.xml", workflow)
        self.assertEqual(
            manifest["artifacts"],
            [
                "SimRV-rv32-linux-x86_64-v3.0.0-beta.2.tar.gz",
                "SimRV-rv64-linux-x86_64-v3.0.0-beta.2.tar.gz",
                "SimRV-rpm-packages-v3.0.0-beta.2.tar.gz",
                "SimRV-deb-packages-v3.0.0-beta.2.tar.gz",
            ],
        )

    def test_native_package_versions_match_release_semver(self):
        version = "3.0.0-beta.2"
        self.assertTrue(release_check.artifact_contains_version("SimRV-v3.0.0-beta.2.tar.gz", version))
        self.assertTrue(release_check.artifact_contains_version("simrv_3.0.0~beta.2_amd64.deb", version))
        self.assertTrue(release_check.artifact_contains_version("simrv-3.0.0-0.beta.2.x86_64.rpm", version))
        self.assertFalse(release_check.artifact_contains_version("simrv-3.0.0-0.beta.1.x86_64.rpm", version))

    def test_configuration_fingerprint_is_stable(self):
        first = {"xlen": 64, "isa": "rv64gc", "vlen": 256}
        second = {"vlen": 256, "isa": "rv64gc", "xlen": 64}
        first_json, first_id = metadata.configuration_fingerprint(first)
        second_json, second_id = metadata.configuration_fingerprint(second)
        self.assertEqual(first_json, second_json)
        self.assertEqual(first_id, second_id)
        self.assertEqual(len(first_id), 16)

    def test_experiment_manifest_declares_stopping_policies(self):
        manifest = json.loads((ROOT / "repro/experiment-manifest.json").read_text())
        ids = [item["id"] for item in manifest["performance"]["workloads"]]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertIn(0, [item["instruction_limit"]
                          for item in manifest["performance"]["workloads"]])

    def test_benchmark_commands_share_isa_and_symmetric_limit(self):
        isa = "rv64gc_zicsr_zifencei_zicntr"
        simrv = benchmark.simrv_benchmark_command(
            "SimRV", ["--quiet"], "guest.elf", 1234, "0x80001000", isa
        )
        spike = benchmark.spike_benchmark_command("spike", "guest.elf", 1234, isa)
        self.assertIn("--isa", simrv)
        self.assertEqual(simrv[simrv.index("--isa") + 1], isa)
        self.assertEqual(spike[1], f"--isa={isa}")
        self.assertEqual(simrv[-2:], ["-e", "1234"])
        self.assertIn("--instructions=1234", spike)

    def test_benchmark_commands_default_to_guest_completion(self):
        isa = "rv64gc_zicsr_zifencei_zicntr"
        simrv = benchmark.simrv_benchmark_command(
            "SimRV", [], "guest.elf", 0, "0x80001000", isa
        )
        spike = benchmark.spike_benchmark_command("spike", "guest.elf", 0, isa)
        self.assertNotIn("-e", simrv)
        self.assertFalse(any(arg.startswith("--instructions=") for arg in spike))

    def test_stopping_policy_uses_complete_commands(self):
        simrv = benchmark.simrv_benchmark_command(
            "SimRV", [], "guest.elf", 1234, "0x80001000", "rv64gc"
        )
        spike = benchmark.spike_benchmark_command("spike", "guest.elf", 1234, "rv64gc")
        self.assertEqual(benchmark.check_stopping_policy_match(simrv, spike), (1234, 1234, False))

        spike[-2] = "--instructions=4321"
        self.assertEqual(benchmark.check_stopping_policy_match(simrv, spike), (1234, 4321, True))

    def test_event_pipe_parser_uses_last_valid_value(self):
        raw = b"decode_cache_hit_rate 10 9990\nmalformed\nmetric 11 nope\ndecode_cache_hit_rate 12 9998\n"
        self.assertEqual(benchmark.parse_event_pipe_data(raw), {"decode_cache_hit_rate": 9998})

    def test_perf_counter_aggregation_skips_unavailable_values(self):
        runs = [
            {"task-clock": {"value": 10.0}, "cycles": {"value": None}},
            {"task-clock": {"value": 14.0}},
        ]
        self.assertEqual(
            benchmark.aggregate_perf_counters(runs),
            {"task-clock": {"mean": 12.0, "min": 10.0, "max": 14.0, "n": 2}},
        )

    def test_command_version_falls_back_to_help(self):
        responses = [
            subprocess.CompletedProcess([], 1, "", "spike: unrecognized option --version\n"),
            subprocess.CompletedProcess([], 0, "Spike RISC-V ISA Simulator 1.1.1-dev\n", ""),
        ]
        with mock.patch.object(benchmark.subprocess, "run", side_effect=responses) as run:
            self.assertEqual(
                benchmark.command_version("spike"), "Spike RISC-V ISA Simulator 1.1.1-dev"
            )
        self.assertEqual(run.call_args_list[1].args[0], ["spike", "--help"])

    def test_native_release_preset_pins_clang(self):
        presets = json.loads((ROOT / "CMakePresets.json").read_text())
        native = next(item for item in presets["configurePresets"]
                      if item["name"] == "rv64-native-release")
        self.assertEqual(native["cacheVariables"]["CMAKE_C_COMPILER"], "clang")
        self.assertEqual(native["cacheVariables"]["CMAKE_CXX_COMPILER"], "clang++")
        self.assertEqual(native["cacheVariables"]["SIMRV_NATIVE_HOST_OPTIMIZATIONS"], "ON")

    def test_benchmark_modes_baremetal_command(self):
        args = types.SimpleNamespace(simrv="simrv", harts=4, os=False, disk=None, dtb=None,
                                     image="guest.elf", limit=1234, tohost="0x80001000",
                                     smp_multithreaded=False)
        command = benchmark_modes.command(args, "cycle-accurate", "5stage", False)
        self.assertEqual(command.count("--baremetal"), 1)
        self.assertNotIn("--os", command)
        self.assertIn("--smp", command)
        self.assertIn("--pipeline", command)
        self.assertEqual(command[-2:], ["-H", "0x80001000"])

    def test_benchmark_modes_os_command(self):
        args = types.SimpleNamespace(simrv="simrv", harts=1, os=True, disk="root.img",
                                     dtb="virt.dtb", image="firmware.bin", limit=5678,
                                     tohost=None, smp_multithreaded=False)
        command = benchmark_modes.command(args, "fast", None, True)
        self.assertEqual(command.count("--os"), 1)
        self.assertNotIn("--baremetal", command)
        self.assertNotIn("-b", command)
        self.assertIn("-D", command)
        self.assertIn("-f", command)
        self.assertNotIn("--pipeline", command)

    def test_inspection_report_schema_is_versioned_and_private(self):
        schema = json.loads((ROOT / "schemas/inspection-report.schema.json").read_text())
        self.assertEqual(schema["properties"]["schema_version"]["const"], 1)
        serialized = json.dumps(schema).lower()
        self.assertNotIn("student_id", serialized)
        self.assertNotIn("grade", serialized)

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
            self.assertEqual(first[0]["median_kips_ci"], second[0]["median_kips_ci"])

    def test_compare_is_evidence_only_by_default(self):
        def report(speed):
            return {"xlen": 64, "test_name": "demo", "simrv": {"stats": {"wall_speed": {"median": speed}}}}
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            baseline, candidate = root / "base.json", root / "candidate.json"
            baseline.write_text(json.dumps(report(100.0)))
            candidate.write_text(json.dumps(report(50.0)))
            result = subprocess.run([sys.executable, str(ROOT / "scripts/benchmark.py"), "compare",
                                     str(baseline), str(candidate)], check=False)
            self.assertEqual(result.returncode, 0)

    def test_perf_stat_parser_preserves_scaling_metadata(self):
        contents = (
            "12345;;cycles;1000000;98.50\n"
            "678;;instructions;1000000;98.50\n"
            "<not supported>;;cache-misses;1000000;100.00\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "perf.csv"
            path.write_text(contents)
            counters = benchmark.parse_perf_stat(path)
        self.assertEqual(counters["cycles"]["value"], 12345)
        self.assertEqual(counters["cycles"]["time_enabled_ns"], 1000000)
        self.assertEqual(counters["cycles"]["running_percent"], 98.5)
        self.assertNotIn("cache-misses", counters)


if __name__ == "__main__":
    unittest.main()
