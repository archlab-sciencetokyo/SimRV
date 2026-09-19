#!/usr/bin/env python3
"""Target-driven RTL/SimRV parity evaluator."""

import argparse
from pathlib import Path
import sys

# Ensure rtl_parity package is resolvable both in-tree and when installed as a standalone binary
_here = Path(__file__).resolve().parent
for _candidate in (
    _here,
    _here / "rtl_parity",
    _here.parent / "share" / "SimRV" / "scripts",
    _here.parent / "scripts",
):
    if (_candidate / "rtl_parity").is_dir():
        if str(_candidate) not in sys.path:
            sys.path.insert(0, str(_candidate))
        break

from rtl_parity import get_target, iter_targets


def create_parser():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("target", nargs="?")
    parser.add_argument("--list-targets", action="store_true")
    parser.add_argument("--help", action="store_true")
    return parser


def print_usage(stream=sys.stderr):
    names = ",".join(target.name for target in iter_targets())
    print(f"usage: evaluate_rtl_parity.py {{{names}}} [target options]", file=stream)


def main(arguments=None):
    known, remaining = create_parser().parse_known_args(arguments)
    if known.list_targets:
        for target in iter_targets():
            print(f"{target.name:8} {target.description}")
        return 0
    target = get_target(known.target)
    if target is None:
        print_usage()
        return 0 if known.help else 2
    if known.help:
        remaining.append("--help")
    return target.run(remaining)


if __name__ == "__main__":
    raise SystemExit(main())
