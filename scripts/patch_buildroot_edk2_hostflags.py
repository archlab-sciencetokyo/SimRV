#!/usr/bin/env python3
"""Keep pinned EDK2 BaseTools compatible with current GCC host defaults."""

from __future__ import annotations

import argparse
from pathlib import Path
import re


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("buildroot_source", type=Path)
    parser.add_argument("edk2_source", type=Path)
    args = parser.parse_args()
    makefile = args.buildroot_source / "boot/edk2/edk2.mk"
    contents = makefile.read_text()
    old = 'EXTRA_OPTFLAGS="$(HOST_CPPFLAGS)"'
    new = 'EXTRA_OPTFLAGS="$(HOST_CPPFLAGS) -std=gnu17 -Wno-unused-but-set-variable"'
    if new not in contents:
        if old not in contents:
            parser.error(f"unexpected Buildroot EDK2 flags in {makefile}")
        makefile.write_text(contents.replace(old, new, 1))

    pccts = args.edk2_source / "BaseTools/Source/C/VfrCompile/Pccts"
    for makefile in pccts.rglob("makefile"):
        contents = makefile.read_text()
        updated = re.sub(
            r"(?m)^(CFLAGS\s*=.*)$",
            lambda match: match.group(1)
            if "-std=gnu17" in match.group(1)
            else match.group(1) + " -std=gnu17",
            contents,
        )
        if contents != updated:
            makefile.write_text(updated)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
