#!/usr/bin/env python3
"""Benchmark SimRV execution profiles with repeatable warmup and sampling."""

import argparse
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
    result += ["--mode", mode]
    if pipeline:
        result += ["--pipeline", pipeline]
    if args.harts > 1:
        result += ["--smp", str(args.harts)]
    if args.os:
        result += ["--os", "-D", args.disk]
        if args.dtb:
            result += ["-f", args.dtb]
    result += ["-m", args.image, "-e", str(args.limit), "-b"]
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


def run_cli(cmd, timeout):
    usage_before = resource.getrusage(resource.RUSAGE_CHILDREN)
    started = time.perf_counter()
    proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                          timeout=timeout, check=False)
    elapsed = time.perf_counter() - started
    usage_after = resource.getrusage(resource.RUSAGE_CHILDREN)
    if proc.returncode != 0:
        raise RuntimeError(f"command failed ({proc.returncode}): {' '.join(cmd)}")
    return {
        "wall_seconds": elapsed,
        "cpu_seconds": ((usage_after.ru_utime + usage_after.ru_stime) -
                        (usage_before.ru_utime + usage_before.ru_stime)),
        "sim_kips": parse_speed(proc.stdout),
        "terminal_bytes": len(proc.stdout),
        "frames": 0,
        "changed_rows": 0,
        "interaction_latency_ms": None,
    }


def run_tui(cmd, timeout, columns, rows, instruction_limit):
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", rows, columns, 0, 0))
    usage_before = resource.getrusage(resource.RUSAGE_CHILDREN)
    started = time.perf_counter()
    proc = subprocess.Popen(cmd, stdin=slave, stdout=slave, stderr=slave, close_fds=True)
    os.close(slave)
    os.set_blocking(master, False)
    output = bytearray()
    run_sent_at = None
    last_resume_at = None
    first_frame_at = None
    quit_sent = False
    last_status = None
    deadline = started + timeout
    try:
        while proc.poll() is None and time.perf_counter() < deadline:
            now = time.perf_counter()
            if run_sent_at is None and now - started >= 1.0:
                os.write(master, b"\x10")
                run_sent_at = now
                last_resume_at = now
            elif (run_sent_at is not None and last_status == "PAUSED" and
                  now - (last_resume_at or run_sent_at) >= 0.25):
                os.write(master, b"c")
                last_resume_at = now
            ready, _, _ = select.select([master], [], [], 0.02)
            if ready:
                try:
                    chunk = os.read(master, 1 << 16)
                except BlockingIOError:
                    chunk = b""
                except OSError as error:
                    if error.errno != errno.EIO:
                        raise
                    chunk = b""
                if chunk:
                    output.extend(chunk)
                    if run_sent_at is None and b"\x1b[?25l" in chunk:
                        os.write(master, b"\x10")
                        run_sent_at = time.perf_counter()
                        last_resume_at = run_sent_at
                        last_status = "PAUSED"
                    elif run_sent_at is not None and first_frame_at is None and b"\x1b[?25l" in chunk:
                        first_frame_at = time.perf_counter()
                    latest_frame = bytes(output[output.rfind(b"\x1b[?25l"):])
                    plain_frame = ANSI_RE.sub(b"", latest_frame)
                    status = ("RUNNING" if b"RUNNING" in plain_frame else
                              "PAUSED" if b"PAUSED" in plain_frame else None)
                    if (run_sent_at is not None and status == "PAUSED" and
                            (last_status != "PAUSED" or
                             now - (last_resume_at or run_sent_at) >= 0.25)):
                        os.write(master, b"c")
                        last_resume_at = now
                    if status is not None:
                        last_status = status
            if not quit_sent and b"finished by -e option" in output:
                os.write(master, b"q")
                quit_sent = True
        if proc.poll() is None:
            proc.terminate()
            proc.wait(timeout=2)
            tail = ANSI_RE.sub(b"", bytes(output[-2000:])).decode("utf-8", "replace")
            raise TimeoutError(f"TUI benchmark timed out: {' '.join(cmd)}\n{tail}")
        while True:
            try:
                chunk = os.read(master, 1 << 16)
                if not chunk:
                    break
                output.extend(chunk)
            except BlockingIOError:
                break
            except OSError as error:
                if error.errno != errno.EIO:
                    raise
                break
    finally:
        try:
            os.close(master)
        except OSError:
            pass
    elapsed = time.perf_counter() - started
    usage_after = resource.getrusage(resource.RUSAGE_CHILDREN)
    raw = bytes(output)
    if proc.returncode != 0:
        raise RuntimeError(f"TUI command failed ({proc.returncode}): {' '.join(cmd)}")
    execution_seconds = elapsed if run_sent_at is None else time.perf_counter() - run_sent_at
    sim_kips = parse_speed(raw)
    if sim_kips is None and execution_seconds > 0:
        sim_kips = instruction_limit / execution_seconds / 1000.0
    return {
        "wall_seconds": elapsed,
        "execution_seconds": execution_seconds,
        "cpu_seconds": ((usage_after.ru_utime + usage_after.ru_stime) -
                        (usage_before.ru_utime + usage_before.ru_stime)),
        "sim_kips": sim_kips,
        "terminal_bytes": len(raw),
        "frames": raw.count(b"\x1b[?25l"),
        "changed_rows": len(re.findall(rb"\x1b\[\d+;1H", raw)),
        "interaction_latency_ms": ((first_frame_at - run_sent_at) * 1000.0
                                   if first_frame_at and run_sent_at else None),
    }


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
        metric = "execution_seconds" if mode.endswith("tui") else "wall_seconds"
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
    report = {"schema": 2, "limit": args.limit, "harts": args.harts,
              "terminal": [args.columns, args.rows], "modes": {}}
    for name, mode, pipeline, tui in modes:
        cmd = command(args, mode, pipeline, tui)
        for _ in range(args.warmup):
            if tui:
                run_tui(cmd, args.timeout, args.columns, args.rows, args.limit)
            else:
                run_cli(cmd, args.timeout)
        samples = []
        for _ in range(args.runs):
            samples.append(run_tui(cmd, args.timeout, args.columns, args.rows, args.limit)
                           if tui else run_cli(cmd, args.timeout))
        report["modes"][name] = summarize(samples)
        print(f"{name:7} {report['modes'][name]['wall_seconds']['median']:.4f}s median")

    if "fast-cli" in report["modes"] and "fast-tui" in report["modes"]:
        cli_seconds = report["modes"]["fast-cli"]["wall_seconds"]["median"]
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
