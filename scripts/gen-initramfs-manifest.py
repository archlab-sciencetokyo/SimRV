#!/usr/bin/env python3
"""Emit gen_init_cpio entries for an initramfs directory tree."""

import os
import stat
import sys


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} INITRAMFS_DIR", file=sys.stderr)
        return 2

    initramfs_dir = sys.argv[1]
    for root, dirs, files in os.walk(initramfs_dir, followlinks=False):
        dirs.sort()
        files.sort()
        rel_root = os.path.relpath(root, initramfs_dir)
        if rel_root == ".":
            rel_root = ""

        # /dev nodes are emitted as fixed entries by the caller.
        if rel_root == "dev" or rel_root.startswith("dev/"):
            dirs.clear()
            continue

        if rel_root:
            mode = f"0{oct(stat.S_IMODE(os.stat(root).st_mode))[2:]}"
            print(f"dir /{rel_root} {mode} 0 0")

        for fname in files:
            fpath = os.path.join(root, fname)
            rel_path = os.path.relpath(fpath, initramfs_dir)
            if rel_path.startswith("dev/"):
                continue
            try:
                file_stat = os.lstat(fpath)
            except OSError:
                continue

            mode = f"0{oct(stat.S_IMODE(file_stat.st_mode))[2:]}"
            if stat.S_ISLNK(file_stat.st_mode):
                print(f"slink /{rel_path} {os.readlink(fpath)} {mode} 0 0")
            elif stat.S_ISREG(file_stat.st_mode):
                print(f"file /{rel_path} {fpath} {mode} 0 0")
            elif stat.S_ISCHR(file_stat.st_mode):
                print(
                    f"nod /{rel_path} {mode} 0 0 c "
                    f"{os.major(file_stat.st_rdev)} {os.minor(file_stat.st_rdev)}"
                )
            elif stat.S_ISBLK(file_stat.st_mode):
                print(
                    f"nod /{rel_path} {mode} 0 0 b "
                    f"{os.major(file_stat.st_rdev)} {os.minor(file_stat.st_rdev)}"
                )
    return 0


if __name__ == "__main__":
    sys.exit(main())
