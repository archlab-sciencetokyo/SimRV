#!/usr/bin/env python3
"""Check whether an existing published kernel matches the requested build."""

import hashlib
import json
import sys


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    if len(sys.argv) != 9:
        print(
            f"Usage: {sys.argv[0]} MANIFEST VMLINUX ARCH LINUX_VERSION PROFILE ROOTFS INITRAMFS_SHA256 CONFIG_SHA256",
            file=sys.stderr,
        )
        return 2

    manifest_path, vmlinux_path, arch, linux_version, profile, rootfs, initramfs_hash, config_hash = sys.argv[1:]
    try:
        with open(manifest_path, encoding="utf-8") as source:
            manifest = json.load(source)
        expected = {
            "architecture": arch,
            "linux_version": linux_version,
            "profile": profile,
            "rootfs": rootfs,
            "initramfs_sha256": initramfs_hash,
            "kernel_config_sha256": config_hash,
        }
        for key, value in expected.items():
            if manifest.get(key) != value:
                print(f"Cannot reuse kernel: {key} differs or is absent.")
                return 1
        if manifest.get("artifacts", {}).get("vmlinux") != sha256(vmlinux_path):
            print("Cannot reuse kernel: published vmlinux is missing or does not match its manifest.")
            return 1
    except (OSError, ValueError, KeyError) as error:
        print(f"Cannot reuse kernel: {error}.")
        return 1

    print("Existing vmlinux matches this architecture, profile, initramfs, and kernel configuration.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
