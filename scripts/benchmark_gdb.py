#!/usr/bin/env python3
"""Compare five paired CLI/GDB runs of the same bounded instruction loop."""

import argparse
import json
import os
from pathlib import Path
import re
import selectors
import socket
import statistics
import subprocess
import tempfile
import time


def run(binary, guest, instructions, debugger, trace=None):
    command = ["stdbuf", "-oL", binary, "--cli", "--mode", "fast", "-m", str(guest),
               "--ram-size", "33554432", "--net", "none", "-e", str(instructions)]
    if debugger:
        command += ["--gdb", "--gdb-port", "0"]
    if trace:
        command = ["strace", "-ff", "-o", trace, "-e", "trace=network,poll,read,write"] + command
    started = time.perf_counter()
    process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT)
    client = None
    try:
        if debugger:
            selector = selectors.DefaultSelector()
            selector.register(process.stdout, selectors.EVENT_READ)
            output = b""
            deadline = time.monotonic() + 10
            while not (match := re.search(rb"GDB server listening on port (\d+)", output)):
                if not selector.select(max(0, deadline - time.monotonic())):
                    raise RuntimeError("GDB listener readiness timed out")
                chunk = os.read(process.stdout.fileno(), 8192)
                if not chunk:
                    raise RuntimeError(output.decode(errors="replace"))
                output += chunk
            selector.close()
            client = socket.create_connection(("127.0.0.1", int(match[1])), timeout=5)
            client.sendall(b"$c#63")
        output, _ = process.communicate(timeout=60)
        if process.returncode:
            raise RuntimeError(output.decode(errors="replace"))
        return instructions / (time.perf_counter() - started)
    finally:
        if client:
            client.close()
        if process.poll() is None:
            process.kill()
            process.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--simrv", default="build/rv64-release/SimRV")
    parser.add_argument("--instructions", type=int, default=100_000_000)
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--trace-prefix", help="Record a separate short GDB syscall trace")
    args = parser.parse_args()
    if hasattr(os, "sched_getaffinity"):
        os.sched_setaffinity(0, {min(os.sched_getaffinity(0))})
    with tempfile.TemporaryDirectory(prefix="simrv-gdb-bench-") as directory:
        guest = Path(directory) / "loop.bin"
        # addi x1,x1,1; jal x0,-4 (identical on RV32 and RV64).
        guest.write_bytes(bytes.fromhex("938010006ff0dfff"))
        samples = {"cli": [], "gdb": []}
        for iteration in range(args.runs):
            for name in (("cli", "gdb") if iteration % 2 == 0 else ("gdb", "cli")):
                samples[name].append(run(args.simrv, guest, args.instructions, name == "gdb"))
        medians = {name: statistics.median(values) for name, values in samples.items()}
        regression = 100 * (1 - medians["gdb"] / medians["cli"])
        result = {"instructions": args.instructions, "runs": args.runs,
                  "instructions_per_second": samples, "medians": medians,
                  "regression_percent": regression, "passes_5_percent_limit": regression <= 5}
        print(json.dumps(result, indent=2))
        if args.json:
            args.json.write_text(json.dumps(result, indent=2) + "\n")
        if args.trace_prefix:
            run(args.simrv, guest, 1_000_000, True, args.trace_prefix)
        return 0 if regression <= 5 else 1


if __name__ == "__main__":
    raise SystemExit(main())
