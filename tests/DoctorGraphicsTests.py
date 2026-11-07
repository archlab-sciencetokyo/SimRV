#!/usr/bin/env python3
"""Check doctor graphics diagnostics with isolated terminal environments."""

import os
import subprocess
import sys


def check(binary, variables, sixel, kitty):
    environment = os.environ.copy()
    for name in ("TERM", "COLORTERM", "TERM_PROGRAM", "WT_SESSION", "KITTY_PID",
                 "KITTY_WINDOW_ID", "TMUX", "SIMRV_GRAPHICS", "SIMRV_ENABLE_DISPLAY"):
        environment.pop(name, None)
    environment.update(variables)
    result = subprocess.run([binary, "--doctor"], env=environment, capture_output=True,
                            text=True, check=True)
    lines = result.stdout.splitlines()
    for label, expected in (("Sixel graphics", sixel), ("Kitty graphics", kitty)):
        line = next((line for line in lines if label in line), "")
        status = line.partition(":")[2].strip()
        assert status.startswith(expected), f"{variables}: expected {expected!r} in {line!r}"


if __name__ == "__main__":
    simrv = sys.argv[1]
    check(simrv, {"TERM": "dumb", "SIMRV_GRAPHICS": "auto"}, "not detected", "not detected")
    check(simrv, {"TERM": "dumb", "KITTY_WINDOW_ID": "1"}, "not detected", "detected")
    check(simrv, {"TERM": "dumb", "KITTY_WINDOW_ID": "1", "SIMRV_GRAPHICS": "off"},
          "disabled by override", "disabled by override")
    check(simrv, {"TERM": "dumb", "SIMRV_GRAPHICS": "sixel"}, "enabled by override", "not detected")
    check(simrv, {"TERM": "dumb", "SIMRV_GRAPHICS": "kitty"}, "not detected", "enabled by override")
    check(simrv, {"TERM": "dumb", "SIMRV_GRAPHICS": "off", "SIMRV_ENABLE_DISPLAY": "1"},
          "disabled by override", "disabled by override")
