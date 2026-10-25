#!/usr/bin/env python3
"""Interactively create an editable SimRV classroom mission."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

VIEWS = ("explain", "registers", "stack", "trace")
PANELS = (*VIEWS, "pipeline", "cache", "tlb", "hazard", "branch-predictor", "bus", "console", "display")


def toml_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=False)


def minimum_width(panel_count: int) -> int:
    return (40, 80, 144, 192)[panel_count - 1]


def print_step_preview(index: int, step: dict[str, str], default_speed: int) -> None:
    panels = step["panels"].split(",") if step["panels"] else [step["view"]]
    speed = int(step["speed_hz"] or default_speed)
    speed_text = "maximum speed" if speed == 0 else f"{speed} Hz"
    print(f"  {index}. {step['title']} — {speed_text}; {', '.join(panels)}; "
          f"minimum width {minimum_width(len(panels))}")
    print(f"     Learn: {step['learn'] or step['title']}")
    print(f"     Do: {step['prompt']}")
    if step["hint"]:
        print(f"     Hint: {step['hint']}")
    print(f"     Why: {step['why']}")


def ask_required(prompt: str) -> str:
    while True:
        answer = input(prompt).strip()
        if answer:
            return answer
        print("A value is required.")


def ask_optional(prompt: str) -> str:
    return input(prompt).strip()


def ask_speed(prompt: str, *, optional: bool = False) -> str:
    while True:
        value = ask_optional(prompt)
        if optional and not value:
            return ""
        if value.isdecimal() and 0 <= int(value) <= 1_000_000:
            return str(int(value))
        print("Enter an integer from 0 to 1000000; 0 means maximum speed.")


def create_mission(output: Path) -> bool:
    print("Create a SimRV classroom mission. Steps are instructional prompts; checkpoints are separate program symbols.")
    mission_id = ask_required("Mission id: ")
    program = ask_required("Expected ELF filename: ")
    description = ask_optional("Lesson description (optional): ")
    goal = ask_optional("Learning goal (optional): ")
    mission_speed = ask_speed("Mission default speed in Hz [30]: ", optional=True) or "30"

    steps: list[dict[str, str]] = []
    while True:
        title = input(f"Step #{len(steps) + 1} title (blank to finish): ").strip()
        if not title:
            if steps:
                break
            print("Add at least one step.")
            continue
        prompt = ask_required("  What should the student do or observe? ")
        learn = ask_optional("  What will this step help students learn? (optional): ")
        hint = ask_optional("  Short hint (optional): ")
        why = ask_required("  Why does it matter? ")
        while True:
            panel_text = ask_optional("  Whole panel layout (comma-separated, blank for one view): ").lower()
            panels = [part.strip() for part in panel_text.split(",") if part.strip()]
            if not panel_text or (1 <= len(panels) <= 4 and len(set(panels)) == len(panels) and all(p in PANELS for p in panels)):
                break
            print("Choose one to four unique panel names: " + ", ".join(PANELS))
        view = ""
        if not panels:
            while True:
                view = ask_required(f"  Focus view ({'/'.join(VIEWS)}): ").lower()
                if view in VIEWS:
                    break
                print(f"Choose one of: {', '.join(VIEWS)}.")
        step_speed = ask_speed("  Speed override in Hz (blank inherits lesson speed): ", optional=True)
        glossary = ask_optional("  Glossary topic 0-5 (blank for none): ")
        while glossary and not (glossary.isdecimal() and 0 <= int(glossary) <= 5):
            print("Enter a glossary topic from 0 through 5, or leave it blank.")
            glossary = ask_optional("  Glossary topic 0-5 (blank for none): ")
        steps.append({"title": title, "prompt": prompt, "learn": learn, "hint": hint, "why": why,
                      "view": view, "panels": ",".join(panels), "speed_hz": step_speed,
                      "glossary": glossary})

    checkpoints: list[dict[str, str | int]] = []
    checkpoint_labels: set[str] = set()
    checkpoint_boundaries: set[int] = set()
    print("Add optional checkpoints. Each one gates progress after a numbered step.")
    while True:
        label = input("Checkpoint symbol (blank to finish): ").strip()
        if not label:
            break
        if label in checkpoint_labels:
            print("Each checkpoint symbol must be unique.")
            continue
        while True:
            after_step = input(f"  After which step (1-{len(steps)})? ").strip()
            if after_step.isdecimal() and 1 <= int(after_step) <= len(steps):
                if int(after_step) in checkpoint_boundaries:
                    print("Each step can have at most one checkpoint boundary.")
                    continue
                break
            print(f"Enter a step number from 1 through {len(steps)}.")
        message = ask_optional("  Message shown in the modal when reached (optional): ")
        checkpoint_labels.add(label)
        checkpoint_boundaries.add(int(after_step))
        checkpoints.append({"label": label, "after_step": int(after_step), "message": message})

    lines = [
        "# SimRV classroom mission format, version 1.",
        "[mission]",
        'version = "1"',
        f"id = {toml_string(mission_id)}",
        f"program = {toml_string(program)}",
        f"speed_hz = {toml_string(mission_speed)}",
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
            ]
        )
        if step["hint"]:
            lines.append(f"hint = {toml_string(step['hint'])}")
        if step["learn"]:
            lines.append(f"learn = {toml_string(step['learn'])}")
        if step["view"]:
            lines.append(f"view = {toml_string(step['view'])}")
        if step["panels"]:
            lines.append(f"panels = {toml_string(step['panels'])}")
        if step["speed_hz"] != "":
            lines.append(f"speed_hz = {toml_string(step['speed_hz'])}")
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

    generated = "\n".join(lines) + "\n"
    print("\n--- Lesson flow preview ---")
    print(f"Lesson: {description or mission_id}")
    print(f"Goal: {goal or 'No goal specified'}")
    default_speed = int(mission_speed)
    by_boundary = {int(cp["after_step"]): cp for cp in checkpoints}
    start = 1
    for boundary in sorted(by_boundary):
        print(f"Steps {start}–{boundary} lead to checkpoint {by_boundary[boundary]['label']}.")
        for index in range(start, boundary + 1):
            print_step_preview(index, steps[index - 1], default_speed)
        if by_boundary[boundary]["message"]:
            print(f"   Reached message: {by_boundary[boundary]['message']}")
        start = boundary + 1
    if start <= len(steps):
        print(f"Steps {start}–{len(steps)} have no checkpoint boundary.")
        for index in range(start, len(steps) + 1):
            print_step_preview(index, steps[index - 1], default_speed)
    print("\n--- Editable mission file ---")
    print(generated)
    if input("Save this mission? [Y/n] ").strip().lower() in {"n", "no"}:
        print("Canceled; no file was written.")
        return False
    if output.exists():
        if input(f"{output} exists. Replace it? [y/N] ").strip().lower() not in {"y", "yes"}:
            print("Canceled; no file was written.")
            return False
    output.write_text(generated, encoding="utf-8")
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
