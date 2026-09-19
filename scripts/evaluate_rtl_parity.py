#!/usr/bin/env python3
"""Target-driven RTL/SimRV parity evaluator."""

import argparse
import sys

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
