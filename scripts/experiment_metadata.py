#!/usr/bin/env python3
"""Stable experiment provenance shared by branch-specific benchmark frontends."""

from __future__ import annotations

import hashlib
import json
import pathlib
import platform
import subprocess
from collections.abc import Iterable


def sha256_file(path: str | pathlib.Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def command_version(executable: str | None) -> str:
    if not executable:
        return "unavailable"
    for option in ("--version", "--help"):
        try:
            result = subprocess.run(
                [executable, option], capture_output=True, text=True, timeout=5, check=False
            )
        except (OSError, subprocess.TimeoutExpired):
            continue
        lines = (result.stdout or result.stderr).strip().splitlines()
        if lines and (result.returncode == 0 or option == "--help"):
            return lines[0].strip()
    return "unknown"


def repository_revision(root: str | pathlib.Path) -> str:
    try:
        return subprocess.run(
            ["git", "rev-parse", "HEAD"], cwd=root, capture_output=True, text=True,
            timeout=5, check=True
        ).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return "unknown"


def configuration_fingerprint(configuration: dict) -> tuple[str, str]:
    """Return canonical JSON and a short SHA-256 identity for an experiment configuration."""
    canonical = json.dumps(configuration, sort_keys=True, separators=(",", ":"))
    return canonical, hashlib.sha256(canonical.encode("utf-8")).hexdigest()[:16]


def experiment_provenance(
    *, root: str | pathlib.Path, simrv: str, spike: str | None, workload: str,
    configuration: dict, simrv_command: Iterable[str], spike_command: Iterable[str] | None,
) -> dict:
    canonical, fingerprint = configuration_fingerprint(configuration)
    return {
        "schema_version": 1,
        "configuration": configuration,
        "configuration_canonical": canonical,
        "configuration_fingerprint": fingerprint,
        "simrv": {
            "path": str(pathlib.Path(simrv).resolve()),
            "sha256": sha256_file(simrv),
            "version": command_version(simrv),
            "revision": repository_revision(root),
            "command": list(simrv_command),
        },
        "reference": {
            "path": str(pathlib.Path(spike).resolve()) if spike else None,
            "sha256": sha256_file(spike) if spike else None,
            "version": command_version(spike),
            "command": list(spike_command) if spike_command else None,
        },
        "workload": {
            "path": str(pathlib.Path(workload).resolve()),
            "sha256": sha256_file(workload),
        },
        "host": platform.platform(),
    }
