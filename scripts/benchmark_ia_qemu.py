#!/usr/bin/env python3
"""Compare SimRV IA mode with QEMU TCG using one full-system workload."""

import argparse, fcntl, hashlib, json, os, pty, re, select, statistics, struct
import subprocess, tempfile, termios, time

ANSI_RE = re.compile(rb"\x1b(?:\[[0-?]*[ -/]*[@-~]|\][^\x07]*(?:\x07|\x1b\\))")


def digest(path):
    with open(path, "rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def wait_for(master, output, needle, deadline):
    while time.monotonic() < deadline:
        ready, _, _ = select.select([master], [], [], 0.05)
        if not ready:
            continue
        try:
            chunk = os.read(master, 65536)
        except OSError:
            chunk = b""
        output.extend(chunk)
        if needle in ANSI_RE.sub(b"", bytes(output[-262144:])):
            return time.monotonic()
    raise TimeoutError(f"timed out waiting for {needle!r}")


def run_engine(name, command, workload, warmups, runs, boot_timeout, run_timeout):
    master, slave = pty.openpty()
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 48, 160, 0, 0))
    started = time.monotonic()
    proc = subprocess.Popen(command, stdin=slave, stdout=slave, stderr=slave)
    os.close(slave)
    os.set_blocking(master, False)
    output, samples = bytearray(), []
    try:
        shell = wait_for(master, output, b"~ #", started + boot_timeout)
        os.write(master, b'echo __BENCH_"READY__"\r')
        wait_for(master, output, b"__BENCH_READY__", time.monotonic() + 10)
        for index in range(warmups + runs):
            token = f"{index:02d}".encode()
            line = (b'echo __BENCH_"START_' + token + b'__"; ' + workload.encode() +
                    b'; echo __BENCH_"DONE_' + token + b'__"\r')
            os.write(master, line)
            begin = wait_for(master, output, b"__BENCH_START_" + token + b"__",
                             time.monotonic() + 10)
            end = wait_for(master, output, b"__BENCH_DONE_" + token + b"__",
                           begin + run_timeout)
            if index >= warmups:
                samples.append(end - begin)
        return {"command": command, "boot_seconds": shell - started,
                "samples_seconds": samples, "median_seconds": statistics.median(samples),
                "mean_seconds": statistics.mean(samples),
                "cv_percent": (statistics.stdev(samples) / statistics.mean(samples) * 100
                               if len(samples) > 1 else 0.0)}
    finally:
        if proc.poll() is None:
            os.write(master, b"\x01x" if name == "qemu" else b"\x11")
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.terminate()
                try:
                    proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    proc.kill(); proc.wait()
        os.close(master)


def render(report):
    simrv, qemu = report["engines"]["simrv-ia"], report["engines"]["qemu-tcg"]
    ratio = simrv["median_seconds"] / qemu["median_seconds"]
    winner = "QEMU faster" if ratio >= 1 else "SimRV faster"
    return "\n".join([
        "# SimRV IA vs QEMU TCG benchmark", "",
        f"Guest workload: `{report['workload']}`", "",
        "Boot time is separate and excluded from workload timing. Both engines use the same "
        "firmware and root filesystem; QEMU uses single-threaded TCG.", "",
        "| Engine | Boot (s) | Workload median (s) | Mean (s) | CV |",
        "|---|---:|---:|---:|---:|",
        f"| SimRV IA | {simrv['boot_seconds']:.3f} | {simrv['median_seconds']:.4f} | {simrv['mean_seconds']:.4f} | {simrv['cv_percent']:.2f}% |",
        f"| QEMU TCG | {qemu['boot_seconds']:.3f} | {qemu['median_seconds']:.4f} | {qemu['mean_seconds']:.4f} | {qemu['cv_percent']:.2f}% |",
        "", f"SimRV IA / QEMU TCG elapsed-time ratio: **{ratio:.2f}x** ({winner}).", ""])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--simrv", default="./build/rv64-release/SimRV")
    parser.add_argument("--qemu", default="qemu-system-riscv64")
    parser.add_argument("--firmware", default="linux-images/rv64/fw_payload.bin")
    parser.add_argument("--disk", default="linux-images/rv64/root.img")
    parser.add_argument("--dtb", default="linux-images/rv64/devicetree.dtb")
    parser.add_argument("--workload", default="dd if=/dev/zero bs=1M count=4 2>/dev/null | gzip -1 >/dev/null")
    parser.add_argument("--warmups", type=int, default=1)
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--boot-timeout", type=float, default=120)
    parser.add_argument("--run-timeout", type=float, default=120)
    parser.add_argument("--json", default="benchmark-ia-vs-qemu.json")
    parser.add_argument("--markdown", default="benchmark-ia-vs-qemu.md")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="simrv-ia-qemu-") as temp:
        qemu_disk = os.path.join(temp, "root.img")
        subprocess.run(["cp", "--reflink=auto", args.disk, qemu_disk], check=True)
        simrv_cmd = [args.simrv, "--cli", "--os", "--mode", "fast", "-m", args.firmware,
                     "-D", args.disk, "-f", args.dtb, "-e", "100000000000"]
        qemu_cmd = [args.qemu, "-machine", "virt", "-accel", "tcg,thread=single",
                    "-nographic", "-bios", args.firmware, "-m", "256M",
                    "-drive", f"file={qemu_disk},format=raw,if=virtio"]
        engines = {"simrv-ia": run_engine("simrv", simrv_cmd, args.workload, args.warmups,
                                          args.runs, args.boot_timeout, args.run_timeout),
                   "qemu-tcg": run_engine("qemu", qemu_cmd, args.workload, args.warmups,
                                          args.runs, args.boot_timeout, args.run_timeout)}
    report = {"schema": 1, "workload": args.workload, "warmups": args.warmups,
              "runs": args.runs,
              "artifacts": {"simrv_sha256": digest(args.simrv),
                            "firmware_sha256": digest(args.firmware),
                            "disk_sha256": digest(args.disk)}, "engines": engines}
    with open(args.json, "w", encoding="utf-8") as output:
        json.dump(report, output, indent=2); output.write("\n")
    text = render(report)
    with open(args.markdown, "w", encoding="utf-8") as output:
        output.write(text)
    print(text)


if __name__ == "__main__":
    main()
