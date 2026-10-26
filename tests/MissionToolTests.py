#!/usr/bin/env python3
"""Integration checks for educator mission creation and validation tools."""

from __future__ import annotations

import importlib.util
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from io import StringIO
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CHECKER = ROOT / "scripts/check_mission.py"
CREATOR = ROOT / "scripts/create_mission.py"
EXAMPLE_ELF = ROOT / "examples/isa/build-rv64/control-flow-calls.elf"
CHECKER_MODULE_SPEC = importlib.util.spec_from_file_location("mission_checker", CHECKER)
assert CHECKER_MODULE_SPEC and CHECKER_MODULE_SPEC.loader
CHECKER_MODULE = importlib.util.module_from_spec(CHECKER_MODULE_SPEC)
CHECKER_MODULE_SPEC.loader.exec_module(CHECKER_MODULE)
smoke_test_checkpoints = CHECKER_MODULE.smoke_test_checkpoints


def mission_text(*, speed: str = "30", step_speed: str = "", panels: str = "registers,trace",
                checkpoint: str = "marker", after_step: str = "1") -> str:
    step_speed_line = f'speed_hz = "{step_speed}"\n' if step_speed else ""
    return (
        '[mission]\nversion = "1"\nid = "smoke"\nprogram = "demo.elf"\n'
        f'speed_hz = "{speed}"\n\n[step]\nid = "one"\ntitle = "Observe"\n'
        'prompt = "Inspect state"\nhint = "Check the destination register."\n'
        'why = "The state shows the result."\n'
        f'panels = "{panels}"\n{step_speed_line}\n'
        f'[checkpoint]\nlabel = "{checkpoint}"\nafter_step = {after_step}\n'
        'message = "Reached."\n'
    )


class MissionToolTests(unittest.TestCase):
    def run_checker(self, mission: Path, *args: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(CHECKER), str(mission), *args],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )

    def test_checker_preview_reports_effective_speed_layout_and_checkpoint(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            mission = Path(directory) / "lesson.mission"
            mission.write_text(mission_text(step_speed="0"), encoding="utf-8")
            result = self.run_checker(mission, "--preview")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("1. Observe [one] — maximum speed (0 Hz)", result.stdout)
        self.assertIn("Panels: registers, trace (minimum width 80 columns)", result.stdout)
        self.assertIn("checkpoint marker", result.stdout)
        self.assertIn("Reached message: Reached.", result.stdout)

    def test_checker_rejects_invalid_speed_panels_and_boundaries(self) -> None:
        cases = (
            (mission_text(speed="1000001"), "speed_hz"),
            (mission_text(step_speed="1000001"), "speed_hz"),
            (mission_text(panels="registers,unknown"), "panels"),
            (mission_text(panels="registers,live"), "panels"),
            (mission_text(panels="registers,registers"), "panels"),
            (mission_text(after_step="2"), "after_step"),
        )
        for contents, expected_error in cases:
            with self.subTest(expected_error=expected_error, contents=contents):
                with tempfile.TemporaryDirectory() as directory:
                    mission = Path(directory) / "lesson.mission"
                    mission.write_text(contents, encoding="utf-8")
                    result = self.run_checker(mission)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(expected_error, result.stderr)

    def test_checker_reports_checkpoint_symbol_missing_from_elf(self) -> None:
        if not EXAMPLE_ELF.is_file():
            self.skipTest("example ELF is not built")
        with tempfile.TemporaryDirectory() as directory:
            mission = Path(directory) / "lesson.mission"
            mission.write_text(mission_text(checkpoint="certainly_not_a_real_symbol"), encoding="utf-8")
            result = self.run_checker(mission, "--program", str(EXAMPLE_ELF))
        self.assertEqual(result.returncode, 1)
        self.assertIn("certainly_not_a_real_symbol", result.stdout)

    def test_creator_emits_hint_speed_and_panels_only_step_and_previews_before_save(self) -> None:
        inputs = "\n".join(
            [
                "created",
                "demo.elf",
                "",
                "",
                "25",
                "Observe",
                "Inspect the result",
                "Connect the observed register change to the instruction result",
                "Check the destination register",
                "The register contains the result",
                "registers,trace",
                "0",
                "",
                "",
                "marker",
                "1",
                "Checkpoint reached",
                "",
                "",
                "",
            ]
        )
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "created.mission"
            created = subprocess.run(
                [sys.executable, str(CREATOR), "--output", str(output)],
                cwd=ROOT,
                input=inputs,
                text=True,
                capture_output=True,
                check=False,
            )
            self.assertEqual(created.returncode, 0, created.stderr)
            self.assertIn("--- Lesson flow preview ---", created.stdout)
            self.assertIn("Learn: Connect the observed register change", created.stdout)
            self.assertIn("Do: Inspect the result", created.stdout)
            self.assertIn("minimum width 80", created.stdout)
            contents = output.read_text(encoding="utf-8")
            self.assertIn('learn = "Connect the observed register change', contents)
            self.assertIn('hint = "Check the destination register"', contents)
            self.assertIn('speed_hz = "25"', contents)
            self.assertIn('panels = "registers,trace"', contents)
            checked = self.run_checker(output, "--preview")
        self.assertEqual(checked.returncode, 0, checked.stderr)
        self.assertIn("OK: mission 'created'", checked.stdout)
        self.assertIn("Steps 1–1 → checkpoint marker", checked.stdout)

    def test_checker_rejects_duplicate_step_ids(self) -> None:
        contents = mission_text().replace(
            '[checkpoint]\nlabel = "marker"',
            '[step]\nid = "one"\ntitle = "Second"\nprompt = "Inspect again"\n'
            'why = "The state changes."\nview = "trace"\n\n'
            '[checkpoint]\nlabel = "marker"',
        )
        with tempfile.TemporaryDirectory() as directory:
            mission = Path(directory) / "lesson.mission"
            mission.write_text(contents, encoding="utf-8")
            result = self.run_checker(mission)
        self.assertEqual(result.returncode, 1)
        self.assertIn("duplicates step id", result.stderr)

    def test_preview_groups_instructions_until_checkpoint_boundary(self) -> None:
        contents = mission_text(after_step="2").replace(
            "[checkpoint]", '[step]\nid = "two"\ntitle = "Compare"\n'
            'prompt = "Compare the state"\nwhy = "It changed."\nview = "trace"\n\n[checkpoint]'
        )
        with tempfile.TemporaryDirectory() as directory:
            mission = Path(directory) / "grouped.mission"
            mission.write_text(contents, encoding="utf-8")
            result = self.run_checker(mission, "--preview")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("Steps 1–2 → checkpoint marker", result.stdout)
        self.assertIn("1. Observe [one]", result.stdout)
        self.assertIn("2. Compare [two]", result.stdout)

    def test_smoke_test_reports_ordered_reached_and_unreached_checkpoints(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            simrv = root / "fake-simrv"
            simrv.write_text(
                "#!/usr/bin/env python3\n"
                "import json, pathlib, sys\n"
                "trace = pathlib.Path(sys.argv[sys.argv.index('--arch-trace') + 1])\n"
                "trace.write_text('\\n'.join(json.dumps({'event':'retire','pc':pc}) "
                "for pc in ['0x100','0x200']) + '\\n')\n",
                encoding="utf-8",
            )
            simrv.chmod(0o755)
            checkpoints = [
                {"label": "first", "after_step": 1},
                {"label": "second", "after_step": 2},
            ]
            output = StringIO()
            with redirect_stdout(output):
                result = smoke_test_checkpoints(
                    simrv, root / "guest.elf", checkpoints,
                    {"first": 0x100, "second": 0x200}, 100, 5,
                )
            self.assertEqual(result, 0)
            self.assertIn("OK: checkpoint first after step 1 was reached", output.getvalue())
            self.assertIn("OK: checkpoint second after step 2 was reached", output.getvalue())

            output = StringIO()
            with redirect_stdout(output):
                result = smoke_test_checkpoints(
                    simrv, root / "guest.elf", checkpoints,
                    {"first": 0x100, "second": 0x300}, 100, 5,
                )
            self.assertEqual(result, 1)
            self.assertIn("was not reached in order", output.getvalue())
            self.assertIn("increase --max-instructions", output.getvalue())


if __name__ == "__main__":
    unittest.main()
