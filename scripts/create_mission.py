#!/usr/bin/env python3
"""Interactively create an editable SimRV classroom mission."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

VIEWS = ("explain", "registers", "stack", "trace")
PANELS = (*VIEWS, "pipeline", "cache", "tlb", "hazard", "branch-predictor", "bus", "console", "display", "live")


def toml_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def ask_required(prompt: str) -> str:
    while True:
        answer = input(prompt).strip()
        if answer:
            return answer
        print("A value is required.")


def ask_optional(prompt: str) -> str:
    return input(prompt).strip()


def create_mission(output: Path) -> bool:
    print("Create a SimRV classroom mission. Steps are instructional prompts; checkpoints are separate program symbols.")
    mission_id = ask_required("Mission id: ")
    program = ask_required("Expected ELF filename: ")
    description = ask_optional("Lesson description (optional): ")
    goal = ask_optional("Learning goal (optional): ")

    steps: list[dict[str, str]] = []
    while True:
        title = input(f"Step #{len(steps) + 1} title (blank to finish): ").strip()
        if not title:
            if steps:
                break
            print("Add at least one step.")
            continue
        prompt = ask_required("  What should the student do or observe? ")
        why = ask_required("  Why does it matter? ")
        while True:
            view = ask_required(f"  Focus view ({'/'.join(VIEWS)}): ").lower()
            if view in VIEWS:
                break
            print(f"Choose one of: {', '.join(VIEWS)}.")
        while True:
            panel_text = ask_optional("  Whole panel layout (comma-separated, blank for one view): ").lower()
            panels = [part.strip() for part in panel_text.split(",") if part.strip()]
            if not panel_text or (1 <= len(panels) <= 4 and len(set(panels)) == len(panels) and all(p in PANELS for p in panels)):
                break
            print("Choose one to four unique panel names: " + ", ".join(PANELS))
        glossary = ask_optional("  Glossary topic 0-5 (blank for none): ")
        while glossary and not (glossary.isdecimal() and 0 <= int(glossary) <= 5):
            print("Enter a glossary topic from 0 through 5, or leave it blank.")
            glossary = ask_optional("  Glossary topic 0-5 (blank for none): ")
        steps.append({"title": title, "prompt": prompt, "why": why, "view": view, "panels": ",".join(panels), "glossary": glossary})

    checkpoints: list[dict[str, str | int]] = []
    print("Add optional checkpoints. Each one gates progress after a numbered step.")
    while True:
        label = input("Checkpoint symbol (blank to finish): ").strip()
        if not label:
            break
        while True:
            after_step = input(f"  After which step (1-{len(steps)})? ").strip()
            if after_step.isdecimal() and 1 <= int(after_step) <= len(steps):
                break
            print(f"Enter a step number from 1 through {len(steps)}.")
        message = ask_optional("  Message shown in the modal when reached (optional): ")
        checkpoints.append({"label": label, "after_step": int(after_step), "message": message})

    lines = [
        "# SimRV classroom mission format, version 1.",
        "[mission]",
        'version = "1"',
        f"id = {toml_string(mission_id)}",
        f"program = {toml_string(program)}",
    ]
    if description:
        lines.append(f"description = {toml_string(description)}")
    if goal:
        lines.append(f"goal = {toml_string(goal)}")
    for index, step in enumerate(steps, 1):
        lines.extend(
            [
                "",
                "[step]",
                f'id = {toml_string(f"step-{index}")}',
                f"title = {toml_string(step['title'])}",
                f"prompt = {toml_string(step['prompt'])}",
                f"why = {toml_string(step['why'])}",
                f"view = {toml_string(step['view'])}",
            ]
        )
        if step["panels"]:
            lines.append(f"panels = {toml_string(step['panels'])}")
        if step["glossary"]:
            lines.append(f"glossary = {toml_string(step['glossary'])}")
    for checkpoint in checkpoints:
        lines.extend(
            [
                "",
                "[checkpoint]",
                f"label = {toml_string(str(checkpoint['label']))}",
                f"after_step = {checkpoint['after_step']}",
            ]
        )
        if checkpoint["message"]:
            lines.append(f"message = {toml_string(str(checkpoint['message']))}")

    if output.exists():
        if input(f"{output} exists. Replace it? [y/N] ").strip().lower() not in {"y", "yes"}:
            print("Canceled; no file was written.")
            return False
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Created {output}. Run: python3 scripts/check_mission.py {output}")
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", "-o", type=Path, default=Path("lesson.mission"), help="output mission path")
    args = parser.parse_args()
    try:
        return 0 if create_mission(args.output) else 1
    except (EOFError, KeyboardInterrupt):
        print("\nMission creation canceled.", file=sys.stderr)
        return 130
    except OSError as error:
        print(f"Cannot write {args.output}: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
