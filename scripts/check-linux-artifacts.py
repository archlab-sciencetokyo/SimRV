#!/usr/bin/env python3
"""Validate a published SimRV Linux profile and its reproducibility manifest."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import sys


def fail(message: str) -> None:
    raise SystemExit(f"error: {message}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("images_dir", type=pathlib.Path)
    args = parser.parse_args()
    images = args.images_dir
    manifest_path = images / "manifest.json"
    if not manifest_path.is_file():
        fail(f"missing {manifest_path}")
    manifest = json.loads(manifest_path.read_text())

    required = {
        "opensbi-linux-payload.elf",
        "opensbi-dynamic.bin",
        "Image",
        "devicetree.dtb",
        "rootfs.img",
    }
    artifacts = manifest.get("artifacts", {})
    missing = [name for name in required if name not in artifacts or not (images / name).is_file()]
    if missing:
        fail(f"missing required artifacts: {', '.join(sorted(missing))}")
    for name, expected in artifacts.items():
        path = images / name
        if not path.is_file():
            fail(f"manifest lists missing artifact {name}")
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if digest != expected:
            fail(f"SHA-256 mismatch for {name}: {digest} != {expected}")

    bootargs = manifest.get("bootargs", "")
    for token in ("console=ttyS0,115200", "root=LABEL=", "rootfstype=ext4", "rootwait", "rw"):
        if token not in bootargs:
            fail(f"bootargs missing {token!r}")
    if "rdinit=/init" in bootargs and manifest.get("profile") != "debug":
        fail("only the explicit debug profile may publish rdinit=/init")

    dtc = shutil.which("dtc")
    if dtc:
        result = subprocess.run(
            [dtc, "-I", "dtb", "-O", "dts", str(images / "devicetree.dtb")],
            check=True,
            capture_output=True,
            text=True,
        )
        dram_mb = int(manifest["dram_size_mb"])
        dram_hex = f"0x{dram_mb * 1024 * 1024:x}"
        if dram_hex not in result.stdout:
            fail(f"DTB does not contain declared DRAM size {dram_hex}")
        bootargs_match = re.search(r'bootargs = "([^"]+)";', result.stdout)
        if not bootargs_match or bootargs_match.group(1) != bootargs:
            fail("DTB bootargs differ from manifest")

    tune2fs = shutil.which("tune2fs")
    if tune2fs:
        result = subprocess.run([tune2fs, "-l", str(images / "rootfs.img")], capture_output=True, text=True)
        if result.returncode != 0:
            fail("rootfs.img is not a readable ext filesystem")
        if "has_journal" not in result.stdout:
            fail("rootfs.img does not have an ext4 journal")
        label = manifest.get("rootfs_label")
        if label and f"Filesystem volume name:   {label}" not in result.stdout:
            fail(f"rootfs.img label does not match {label!r}")

        uuid_match = re.search(r"Filesystem UUID:\s+([^\n]+)", result.stdout)
        if not uuid_match:
            fail("rootfs.img does not report a filesystem UUID")

        debugfs = shutil.which("debugfs")
        if debugfs and label:
            fstab = subprocess.run(
                [debugfs, "-R", "cat /etc/fstab", str(images / "rootfs.img")],
                capture_output=True,
                text=True,
            ).stdout
            if not re.search(rf"^LABEL={re.escape(label)}\s+/\s+ext4\s", fstab, re.MULTILINE):
                fail("/etc/fstab does not mount the published rootfs label")

    print(f"Linux artifact validation passed: {images}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
