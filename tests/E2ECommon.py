#!/usr/bin/env python3
"""Shared test fixtures, execution helpers, and artifact generators for SimRV E2E suites."""

from __future__ import annotations

import json
import os
import pathlib
import subprocess
import sys
import tempfile
from typing import Any, Dict, List, Optional, Tuple, Union

ROOT = pathlib.Path(__file__).resolve().parents[1]


def get_binary_xlen(binary: pathlib.Path) -> int:
    """Determine compile-time target XLEN width from binary version output."""
    try:
        res = subprocess.run([str(binary), "--version"], capture_output=True, text=True, timeout=5)
        if "(RV32)" in res.stdout:
            return 32
    except Exception:
        pass
    return 64


def get_simrv_binary(xlen: int = 64, override: Optional[Union[str, pathlib.Path]] = None) -> pathlib.Path:
    """Resolve the authoritative SimRV binary path for the requested XLEN width."""
    if override:
        path = pathlib.Path(override).resolve()
        if path.is_file() and get_binary_xlen(path) == xlen:
            return path

    env_var = "SIMRV_BIN" if xlen == 64 else "SIMRV32_BIN"
    if env_var in os.environ:
        path = pathlib.Path(os.environ[env_var]).resolve()
        if path.is_file() and get_binary_xlen(path) == xlen:
            return path

    candidates = [
        ROOT / f"build/rv{xlen}-release/simrv",
        ROOT / f"build/rv{xlen}-dev/simrv",
        ROOT / f"build/rv{xlen}-debug/simrv",
        ROOT / f"build/rv{xlen}-native-release/simrv",
        ROOT / f"build/release-gate/run1-gcc-rv{xlen}/simrv",
    ]
    for cand in candidates:
        if cand.is_file() and os.access(cand, os.X_OK):
            return cand

    # Fallback to system search
    cmd = f"simrv{xlen}" if xlen == 32 else "simrv"
    which = shutil_which(cmd)
    if which:
        return pathlib.Path(which).resolve()

    # Default to release preset path even if not yet compiled
    return (ROOT / f"build/rv{xlen}-release/simrv").resolve()


def shutil_which(cmd: str) -> Optional[str]:
    for path in os.environ.get("PATH", "").split(os.pathsep):
        candidate = pathlib.Path(path) / cmd
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return str(candidate)
    return None


def run_simrv(
    binary: pathlib.Path,
    args: List[str],
    *,
    stdin_input: Optional[str] = None,
    timeout: float = 30.0,
    extra_env: Optional[Dict[str, str]] = None,
    cwd: Optional[pathlib.Path] = None,
) -> subprocess.CompletedProcess[str]:
    """Execute a SimRV command and capture output."""
    env = os.environ.copy()
    env["NO_COLOR"] = "1"
    if extra_env:
        env.update(extra_env)

    cmd = [str(binary)] + args
    return subprocess.run(
        cmd,
        input=stdin_input,
        capture_output=True,
        text=True,
        timeout=timeout,
        env=env,
        cwd=str(cwd or ROOT),
        check=False,
    )


def create_instruction_stream(instructions_hex: List[str]) -> bytes:
    """Generate little-endian machine instruction bytes from 32-bit hex strings."""
    data = bytearray()
    for inst_hex in instructions_hex:
        val = int(inst_hex, 16)
        data.extend(val.to_bytes(4, byteorder="little"))
    return bytes(data)


def parse_jsonl(path: pathlib.Path) -> List[Dict[str, Any]]:
    """Parse a JSON lines file into a list of dictionaries."""
    records = []
    if not path.is_file():
        return records
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            stripped = line.strip()
            if stripped:
                records.append(json.loads(stripped))
    return records


def find_example_elf(name: str, xlen: int = 64) -> pathlib.Path:
    """Find a pre-compiled example ELF in the repository."""
    candidates = [
        ROOT / f"examples/isa/build-rv{xlen}/{name}.elf",
        ROOT / f"examples/hello/build-rv{xlen}/{name}.elf",
    ]
    for cand in candidates:
        if cand.is_file():
            return cand
    raise FileNotFoundError(f"Could not find example ELF '{name}.elf' for RV{xlen}")
