#!/usr/bin/env python3
"""Synchronize guest capability properties in the checked-in virt DTS templates."""
import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
HEADER = ROOT / "include/simrv/core/GuestCapabilityCatalog.hpp"
CACHE_HEADER = ROOT / "include/simrv/cache/DCache.hpp"


def array(name: str) -> list[str]:
    text = HEADER.read_text()
    match = re.search(rf"{name}\s*\{{(.*?)\}};", text, re.S)
    if not match:
        raise RuntimeError(f"cannot find {name} in {HEADER}")
    return re.findall(r'"([a-z0-9]+)"', match.group(1))


def render(path: pathlib.Path, is_rv64: bool) -> str:
    text = path.read_text()
    exts = array("kBaseGuestExtensions") + array("kPatchableGuestExtensions")
    if not is_rv64:
        rv64_only = {"ssu64xl", "ssnpm", "supm"}
        exts = [ext for ext in exts if ext not in rv64_only]
    for ext in ("zmmul", "zimop", "zaamo", "zalrsc", "za128rs", "zba", "zbb", "zbs", "zca", "zcb", "zcmop"):
        if ext not in exts:
            exts.append(ext)
    if is_rv64:
        exts.append("zcd")
    # Keep the established stable ordering while removing any catalog overlap.
    exts = list(dict.fromkeys(exts))
    line = "            riscv,isa-extensions = " + ", ".join(f'"{ext}"' for ext in exts) + ";"
    text, count = re.subn(r"^\s*riscv,isa-extensions\s*=.*;$", line, text, count=1, flags=re.M)
    if count != 1:
        raise RuntimeError(f"expected one ISA extension property in {path}")
    cache_match = re.search(r"BaseCache<\d+\s*,\s*(\d+)\s*,", CACHE_HEADER.read_text())
    if not cache_match:
        raise RuntimeError(f"cannot derive cache line size from {CACHE_HEADER}")
    block_size = cache_match.group(1)
    block = (f"            riscv,cbom-block-size = <{block_size}>;\n"
             f"            riscv,cbop-block-size = <{block_size}>;\n"
             f"            riscv,cboz-block-size = <{block_size}>;")
    pattern = (r"^\s*riscv,cbom-block-size\s*=.*;\s*\n\s*"
               r"riscv,cbop-block-size\s*=.*;\s*\n\s*riscv,cboz-block-size\s*=.*;")
    if re.search(pattern, text, re.M):
        text = re.sub(pattern, block, text, count=1, flags=re.M)
    elif is_rv64:
        raise RuntimeError(f"cache capability block missing in {path}")
    else:
        text = text.replace(line, line + "\n" + block)
    return text


def main() -> int:
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--write", action="store_true")
    args = parser.parse_args()
    failed = False
    for width in (32, 64):
        path = ROOT / f"scripts/templates/virt-rv{width}.dts"
        expected = render(path, width == 64)
        if expected != path.read_text():
            failed = True
            if args.write:
                path.write_text(expected)
            else:
                print(f"{path.relative_to(ROOT)} is out of sync; run {__file__} --write")
    return 1 if failed and args.check else 0


if __name__ == "__main__":
    sys.exit(main())
