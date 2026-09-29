#!/usr/bin/env python3
"""Exercise POSIX-style shell behavior through SimRV's headless Linux CLI."""

import fcntl
import os
import pty
import select
import struct
import subprocess
import sys
import termios
import time


MARKERS = {
    "pipe": "__SIMRV_PIPE=2__",
    "redir": "__SIMRV_REDIR=firstsecond__",
    "status": "__SIMRV_STATUS=1__",
    "env": "__SIMRV_ENV=works__",
    "subshell": "__SIMRV_SUB=nested__",
    "cwd": "__SIMRV_CWD=/tmp__",
    "sigpipe": "__SIMRV_SIGPIPE=3__",
    "fd": "__SIMRV_FD=open__",
}


COMMANDS = """\
printf '__SIMRV_BEGIN__\\n'
printf 'alpha\\nbeta\\n' | wc -l | tr -d ' '
printf '__SIMRV_PIPE=2__\\n'
printf 'first\\n' > /tmp/simrv-cli-test
printf 'second\\n' >> /tmp/simrv-cli-test
printf '__SIMRV_REDIR='; tr -d '\\n' < /tmp/simrv-cli-test; printf '__\\n'
false
printf '__SIMRV_STATUS=%s__\\n' "$?"
export SIMRV_TEST_VAR=works
printf '__SIMRV_ENV=%s__\\n' "$SIMRV_TEST_VAR"
printf '__SIMRV_SUB=%s__\\n' "$(printf nested)"
( cd /tmp && printf '__SIMRV_CWD=%s__\\n' "$PWD" )
printf '__SIMRV_SIGPIPE=%s__\\n' "$(yes | head -n 3 | wc -l | tr -d ' ')"
if test -t 0; then printf '__SIMRV_TTY=1__\\n'; else printf '__SIMRV_TTY=0__\\n'; fi
printf '__SIMRV_FD=%s__\\n' "$(test -r /proc/self/fd/0 && echo open)"
rm -f /tmp/simrv-cli-test
poweroff -f
"""


def image_paths():
    root = os.environ.get("SIMRV_IMAGES_DIR")
    if not root:
        root = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "linux-images", "rv64")
    mem = os.environ.get("SIMRV_LINUX_MEM_IMG", os.path.join(root, "fw_payload.bin"))
    disk = os.environ.get("SIMRV_LINUX_DISK_IMG")
    if not disk:
        disk = os.path.join(root, "root.img")
        if not os.path.exists(disk):
            disk = os.path.join(root, "root.bin")
    return mem, disk, os.environ.get("SIMRV_LINUX_DTB", "dynamic")


def main():
    simrv = os.environ.get("SIMRV_BIN")
    if not simrv:
        simrv = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "build", "rv64-release", "simrv")
    mem, disk, dtb = image_paths()
    if not os.path.exists(simrv) or not os.path.exists(mem) or not os.path.exists(disk):
        print("Linux CLI smoke prerequisites are unavailable", file=sys.stderr)
        return 2

    transport = os.environ.get("SIMRV_CLI_TRANSPORT", "pty")
    if transport not in ("pty", "pipe"):
        print(f"unsupported SIMRV_CLI_TRANSPORT={transport}", file=sys.stderr)
        return 2
    cmd = [simrv, "--cli", "--os", "-m", mem, "-D", disk, "-e", "2000000000"]
    if dtb not in ("", "dynamic", "NONE") and os.path.exists(dtb):
        cmd.extend(["-f", dtb])
    cmd.extend(os.environ.get("SIMRV_TEST_EXTRA_ARGS", "").split())
    timeout = float(os.environ.get("SIMRV_TEST_TIMEOUT", "180"))

    master = slave = None
    if transport == "pty":
        master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 40, 140, 0, 0))
        proc = subprocess.Popen(cmd, stdin=slave, stdout=slave, stderr=slave, close_fds=True)
        os.close(slave)
        slave = None
        output_fd = master
    else:
        proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, close_fds=True)
        output_fd = proc.stdout.fileno()

    started = time.monotonic()
    output = bytearray()
    try:
        if transport == "pipe":
            proc.stdin.write(COMMANDS.encode())
            proc.stdin.flush()
        else:
            # CLI mode starts running immediately; wait briefly for the shell before sending input.
            time.sleep(0.5)
            os.write(master, COMMANDS.encode())
        while time.monotonic() - started < timeout:
            if proc.poll() is not None and not select.select([output_fd], [], [], 0)[0]:
                break
            readable, _, _ = select.select([output_fd], [], [], 0.25)
            if readable:
                try:
                    chunk = os.read(output_fd, 65536)
                except OSError:
                    break
                if not chunk:
                    break
                output.extend(chunk)
            if b"__SIMRV_FD=open__" in output and proc.poll() is not None:
                break
        text = output.decode("utf-8", errors="replace").replace("\r", "")
        missing = [value for value in MARKERS.values() if value not in text]
        tty_expected = "__SIMRV_TTY=1__" if transport == "pty" else "__SIMRV_TTY=0__"
        if tty_expected not in text:
            missing.append(tty_expected)
        if missing:
            print("Missing CLI markers:", ", ".join(missing), file=sys.stderr)
            print(text[-6000:], file=sys.stderr)
            return 1
        print(f"[PASS] Linux CLI Unix smoke ({transport})")
        return 0
    finally:
        if proc.poll() is None:
            if transport == "pty" and master is not None:
                try:
                    os.write(master, b"\x11")
                except OSError:
                    pass
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.terminate()
                proc.wait(timeout=3)
        if master is not None:
            os.close(master)
        if slave is not None:
            os.close(slave)


if __name__ == "__main__":
    raise SystemExit(main())
