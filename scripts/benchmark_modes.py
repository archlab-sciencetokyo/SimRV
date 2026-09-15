#!/usr/bin/env python3
"""Benchmark SimRV execution profiles with repeatable warmup and sampling."""

import argparse
import hashlib
import tempfile
import shutil
import errno
import fcntl
import json
import os
import pty
import re
import resource
import select
import statistics
import struct
import subprocess
import termios
import time


ANSI_RE = re.compile(rb"\x1b(?:\[[0-?]*[ -/]*[@-~]|\][^\x07]*(?:\x07|\x1b\\))")
SPEED_RE = re.compile(r"Simulation speed\s*:\s*([\d.]+)\s*(MIPS|KIPS)")


def command(args, mode, pipeline, tui):
    result = [args.simrv, "--tui" if tui else "--cli"]
    result += ["--os" if args.os else "--baremetal"]
    result += ["--mode", mode]
    if pipeline:
        result += ["--pipeline", pipeline]
    if args.smp_multithreaded:
        result += ["--smp-multithreaded"]
    if args.harts > 1:
        result += ["--smp", str(args.harts)]
    if args.os:
        result += ["-D", args.disk]
        if args.dtb:
            result += ["-f", args.dtb]
    result += ["-m", args.image, "-e", str(args.limit)]
    if args.tohost:
        result += ["-H", args.tohost]
    return result


def parse_speed(raw):
    plain = ANSI_RE.sub(b"", raw).decode("utf-8", "replace")
    match = SPEED_RE.search(plain)
    if not match:
        return None
    value = float(match.group(1))
    return value * 1000.0 if match.group(2) == "MIPS" else value


def run_session(cmd, timeout, columns=160, rows=48, tui=False, boot=False):
    """Measure child events, not presentation labels or fixed startup sleeps."""
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, columns, 0, 0))
    event_read, event_write = os.pipe2(os.O_NONBLOCK)
    env = dict(os.environ, SIMRV_EVENT_FD=str(event_write))
    usage_before = resource.getrusage(resource.RUSAGE_CHILDREN)
    started = time.monotonic()
    proc = subprocess.Popen(cmd, stdin=slave, stdout=slave, stderr=slave,
                            env=env, pass_fds=(event_write,))
    os.close(slave)
    os.close(event_write)
    os.set_blocking(master, False)
    output = bytearray()
    events = {}
    pending = b""
    milestones = {}
    shell_command_sent = False
    deadline = started + timeout
    try:
        while time.monotonic() < deadline:
            ready, _, _ = select.select([master, event_read], [], [], 0.02)
            for fd in ready:
                try:
                    chunk = os.read(fd, 65536)
                except OSError as error:
                    if error.errno not in (errno.EIO, errno.EAGAIN):
                        raise
                    chunk = b""
                if fd == event_read:
                    pending += chunk
                    while b"\n" in pending:
                        line, pending = pending.split(b"\n", 1)
                        name, ns, retired = line.decode().split()
                        events[name] = {"time": int(ns) / 1e9, "retired": int(retired)}
                        if name == "ready" and tui:
                            os.write(master, b"c")
                else:
                    output.extend(chunk)
            if boot:
                plain = ANSI_RE.sub(b"", bytes(output[-262144:]))
                now = time.monotonic()
                if b"Linux version" in plain:
                    milestones.setdefault("kernel", now)
                if b"~ #" in plain and not shell_command_sent:
                    milestones["shell"] = now
                    os.write(master, b'echo __SIMRV_BENCH_"OK__"\r')
                    shell_command_sent = True
                if b"__SIMRV_BENCH_OK__" in plain:
                    milestones["command"] = now
                    os.write(master, b"\x11")
                    break
            if proc.poll() is not None:
                break
        else:
            raise TimeoutError(f"benchmark timed out: {' '.join(cmd)}")
        if boot:
            proc.wait(timeout=3)
        else:
            proc.wait(timeout=2)
        if proc.returncode != 0:
            raise RuntimeError(f"command failed ({proc.returncode}): {' '.join(cmd)}")
        if "ready" not in events or "resumed" not in events:
            raise RuntimeError("missing simulator-ready/resume acknowledgement")
        if not boot and "stopped" not in events:
            # Drain the event pipe after process exit, which can race the final select.
            pending += os.read(event_read, 65536)
            for line in pending.splitlines():
                name, ns, retired = line.decode().split()
                events[name] = {"time": int(ns) / 1e9, "retired": int(retired)}
        finished = time.monotonic()
        execution_end = milestones.get("command", events.get("stopped", {}).get("time"))
        if execution_end is None:
            raise RuntimeError("missing completion event")
        resumed = events["resumed"]["time"]
        usage_after = resource.getrusage(resource.RUSAGE_CHILDREN)
        raw = bytes(output)
        result = {
            "wall_seconds": finished - started,
            "initialization_seconds": events["ready"]["time"] - started,
            "resume_latency_ms": (resumed - events["ready"]["time"]) * 1000,
            "execution_seconds": execution_end - resumed,
            "teardown_seconds": finished - execution_end,
            "cpu_seconds": ((usage_after.ru_utime + usage_after.ru_stime) -
                            (usage_before.ru_utime + usage_before.ru_stime)),
            "sim_kips": parse_speed(raw),
            "terminal_bytes": len(raw),
            "frames": raw.count(b"\x1b[?25l"),
            "changed_rows": len(re.findall(rb"\x1b\[\d+;1H", raw)),
        }
        result.update({f"{name}_seconds": stamp - resumed
                       for name, stamp in milestones.items()})
        return result
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
        os.close(master)
        os.close(event_read)


def run_cli(cmd, timeout):
    return run_session(cmd, timeout)


def run_tui(cmd, timeout, columns, rows, instruction_limit):
    return run_session(cmd, timeout, columns, rows, tui=True)


def summarize(samples):
    result = {"runs": samples}
    for key in samples[0]:
        values = [sample[key] for sample in samples if sample[key] is not None]
        if values:
            result[key] = {
                "median": statistics.median(values),
                "mean": statistics.mean(values),
                "cv_percent": (statistics.stdev(values) / statistics.mean(values) * 100.0
                               if len(values) > 1 and statistics.mean(values) else 0.0),
            }
    return result


def compare(report, baseline, threshold):
    failures = []
    for mode, current in report["modes"].items():
        previous = baseline.get("modes", {}).get(mode)
        if not previous:
            continue
        metric = "execution_seconds"
        old = previous[metric]["median"]
        new = current[metric]["median"]
        noise = max(previous[metric].get("cv_percent", 0.0),
                    current[metric].get("cv_percent", 0.0)) / 100.0
        allowance = max(threshold, 2.0 * noise)
        regression = new / old - 1.0
        if regression > allowance:
            failures.append(f"{mode}: wall time regressed {regression * 100:.2f}% "
                            f"(allowance {allowance * 100:.2f}%)")
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--simrv", default="./build/rv64-release/SimRV")
    parser.add_argument("--image", required=True)
    parser.add_argument("--boot", action="store_true", help="measure Linux boot through a shell command")
    parser.add_argument("--smp-multithreaded", action="store_true")
    parser.add_argument("--os", action="store_true", help="benchmark the Linux OS runner")
    parser.add_argument("--disk", help="Linux root disk image (required with --os)")
    parser.add_argument("--dtb", help="optional Linux device-tree blob")
    parser.add_argument("--tohost")
    parser.add_argument("--limit", type=int, default=20_000_000)
    parser.add_argument("--harts", type=int, default=1)
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument("--columns", type=int, default=160)
    parser.add_argument("--rows", type=int, default=48)
    parser.add_argument("--modes", default="fast-cli,detailed-cli,cycle3-cli,cycle5-cli",
                        help="comma-separated execution/interaction profiles")
    parser.add_argument("--output", default="benchmark-modes.json")
    parser.add_argument("--baseline")
    parser.add_argument("--regression-threshold", type=float, default=0.03)
    args = parser.parse_args()
    if args.boot:
        args.os = True
        args.limit = 10000000000
    if args.os and not args.disk:
        parser.error("--disk is required with --os")
    if not 1 <= args.harts <= 64:
        parser.error("--harts must be between 1 and 64")

    available_modes = (
        ("fast-cli", "fast", None, False),
        ("fast-tui", "fast", None, True),
        ("detailed-cli", "detailed", None, False),
        ("detailed-tui", "detailed", None, True),
        ("cycle3-cli", "cycle-accurate", "3stage", False),
        ("cycle3-tui", "cycle-accurate", "3stage", True),
        ("cycle5-cli", "cycle-accurate", "5stage", False),
        ("cycle5-tui", "cycle-accurate", "5stage", True),
    )
    requested_modes = {mode.strip() for mode in args.modes.split(",") if mode.strip()}
    unknown_modes = requested_modes - {mode[0] for mode in available_modes}
    if unknown_modes:
        parser.error(f"unknown benchmark mode(s): {', '.join(sorted(unknown_modes))}")
    modes = tuple(mode for mode in available_modes if mode[0] in requested_modes)
    def digest(path):
        with open(path, "rb") as source:
            return hashlib.file_digest(source, "sha256").hexdigest()
    report = {"schema": 3, "simrv_sha256": digest(args.simrv),
              "image_sha256": digest(args.image),
              "disk_sha256": digest(args.disk) if args.os else None,
              "smp_multithreaded": args.smp_multithreaded, "limit": args.limit, "harts": args.harts,
              "terminal": [args.columns, args.rows], "modes": {}}
    for name, mode, pipeline, tui in modes:
        cmd = command(args, mode, pipeline, tui)
        def sample():
            with tempfile.TemporaryDirectory(prefix="simrv-benchmark-") as directory:
                isolated = list(cmd)
                if args.os:
                    disk = os.path.join(directory, "root.img")
                    subprocess.run(["cp", "--reflink=auto", "--sparse=always", args.disk, disk],
                                   check=True)
                    isolated[isolated.index("-D") + 1] = disk
                return run_session(isolated, args.timeout, args.columns, args.rows,
                                   tui=tui, boot=args.boot)
        for _ in range(args.warmup):
            sample()
        samples = [sample() for _ in range(args.runs)]
        report["modes"][name] = summarize(samples)
        print(f"{name:7} {report['modes'][name]['wall_seconds']['median']:.4f}s median")

    if "fast-cli" in report["modes"] and "fast-tui" in report["modes"]:
        cli_seconds = report["modes"]["fast-cli"]["execution_seconds"]["median"]
        tui_seconds = report["modes"]["fast-tui"]["execution_seconds"]["median"]
        parity = cli_seconds / tui_seconds * 100.0 if tui_seconds else 0.0
        print(f"ia sampled-TUI throughput: {parity:.1f}% of CLI")

    with open(args.output, "w", encoding="utf-8") as target:
        json.dump(report, target, indent=2)
        target.write("\n")
    if args.baseline:
        with open(args.baseline, encoding="utf-8") as source:
            failures = compare(report, json.load(source), args.regression_threshold)
        if failures:
            for failure in failures:
                print(f"REGRESSION: {failure}")
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
