#!/usr/bin/env python3
"""
@file test_linux_pty.py
@brief Verify that the TUI terminal can activate and drive the Linux UART shell.
"""
import fcntl
import os
import pty
import re
import select
import struct
import subprocess
import sys
import termios
import time


SHELL_TOKEN = "__SIMRV_TUI_ENTER_OK__"


def visible_text(data, parser_state):
    """Remove ANSI escape sequences while preserving state across read boundaries."""
    output = []
    state = parser_state[0]
    for byte in data:
        if state == "text":
            if byte == 0x1b:
                state = "escape"
            elif byte >= 0x20 or byte in (0x0a, 0x0d, 0x09):
                output.append(chr(byte))
        elif state == "escape":
            state = "csi" if byte == ord("[") else "text"
        elif 0x40 <= byte <= 0x7e:
            state = "text"
    parser_state[0] = state
    return "".join(output)

def main():
    simrv_bin = os.environ.get("SIMRV_BIN")
    if not simrv_bin:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        repo_root = os.path.dirname(script_dir)
        simrv_bin = os.path.join(repo_root, "build", "rv64-release", "SimRV")

    images_dir = os.environ.get("SIMRV_IMAGES_DIR")
    if not images_dir:
        script_dir = os.path.dirname(os.path.abspath(__file__))
        repo_root = os.path.dirname(script_dir)
        images_dir = os.path.join(repo_root, "linux-images", "rv64")

    mem_img = os.environ.get("SIMRV_LINUX_MEM_IMG", os.path.join(images_dir, "fw_payload.bin"))
    disk_img_default = os.path.join(images_dir, "root.img") if os.path.exists(os.path.join(images_dir, "root.img")) else os.path.join(images_dir, "root.bin")
    disk_img = os.environ.get("SIMRV_LINUX_DISK_IMG", disk_img_default)
    dtb_img = os.environ.get("SIMRV_LINUX_DTB", os.path.join(images_dir, "devicetree.dtb"))
    timeout_secs = int(os.environ.get("SIMRV_TEST_TIMEOUT", "90"))

    if not os.path.exists(simrv_bin):
        print(f"Error: SimRV binary not found at '{simrv_bin}'", file=sys.stderr)
        sys.exit(2)

    if not os.path.exists(mem_img):
        print(f"Skip: Linux memory image not found at '{mem_img}'", file=sys.stderr)
        sys.exit(2)

    cmd = [
        simrv_bin,
        "--os",
        "-m", mem_img,
        "-D", disk_img,
        "-f", dtb_img,
        "--tui",
        "-e", "10000000000"  # Leave enough execution time to interact after boot.
    ]

    print(f"Running Linux PTY boot test: {' '.join(cmd)}")
    start_time = time.time()
    verbose = os.environ.get("SIMRV_TEST_VERBOSE") == "1"

    master_fd, slave_fd = pty.openpty()
    fcntl.ioctl(slave_fd, termios.TIOCSWINSZ, struct.pack("HHHH", 40, 140, 0, 0))

    proc = subprocess.Popen(
        cmd,
        stdin=slave_fd,
        stdout=slave_fd,
        stderr=slave_fd,
        close_fds=True
    )
    os.close(slave_fd)

    buffer = ""
    guest_buffer = ""
    ansi_state = ["text"]
    uart_fd = None
    enter_sent = False
    enter_boundary = 0
    command_sent = False
    passed = False
    run_sent = False

    try:
        while True:
            if proc.poll() is not None:
                break

            try:
                watched_fds = [master_fd]
                if uart_fd is not None:
                    watched_fds.append(uart_fd)
                readable, _, _ = select.select(watched_fds, [], [], 0.25)
                if not readable:
                    if time.time() - start_time > timeout_secs:
                        raise TimeoutError
                    continue
                if master_fd in readable:
                    data = os.read(master_fd, 65536)
                    if not data:
                        break
                    if verbose:
                        sys.stdout.write(data.decode("utf-8", errors="replace"))
                        sys.stdout.flush()
                    buffer += visible_text(data, ansi_state)
                    if len(buffer) > 2_000_000:
                        buffer = buffer[-1_000_000:]

                if uart_fd is None:
                    match = re.search(r"/dev/pts/[0-9]+", buffer)
                    if match:
                        uart_fd = os.open(match.group(0), os.O_RDWR | os.O_NONBLOCK)

                if uart_fd is not None and uart_fd in readable:
                    guest_data = os.read(uart_fd, 65536)
                    guest_buffer += guest_data.decode("utf-8", errors="replace")
                    if len(guest_buffer) > 1_000_000:
                        guest_buffer = guest_buffer[-500_000:]

                if not run_sent and uart_fd is not None and "PAUSED" in buffer and "DETACHED" in buffer:
                    # The first frame can arrive while initialize() is still collecting terminal
                    # capability replies. Wait until that bounded probe has returned before typing.
                    time.sleep(0.2)
                    os.write(master_fd, b"c")
                    run_sent = True
                if not enter_sent and "~ #" in guest_buffer:
                    # UART mirroring reaches this PTY just before the TUI parser answers the shell's
                    # trailing CSI 6 n query. Let one render interval deliver that response first.
                    time.sleep(0.1)
                    os.write(master_fd, b"\r")
                    enter_sent = True
                    enter_boundary = len(guest_buffer)
                if enter_sent and not command_sent and "~ #" in guest_buffer[enter_boundary:]:
                    # Keep the exact token out of the echoed command line, so seeing it proves the
                    # shell ran the command instead of merely echoing keyboard input.
                    time.sleep(0.1)
                    os.write(master_fd, b"echo __SIMRV_TUI_ENTER_\"OK__\"\r")
                    command_sent = True
                if command_sent and SHELL_TOKEN in guest_buffer:
                    passed = True
                    break
            except OSError:
                break

            if time.time() - start_time > timeout_secs:
                raise TimeoutError

    except TimeoutError:
        print(f"\nTimeout ({timeout_secs}s) exceeded waiting for interactive TUI shell.",
              file=sys.stderr)
    except Exception as e:
        print(f"\nExecution error: {e}", file=sys.stderr)
    finally:
        if proc.poll() is None:
            try:
                os.write(master_fd, b"\x11")  # Ctrl-Q: clean TUI shutdown
                proc.wait(timeout=3)
            except (OSError, subprocess.TimeoutExpired):
                proc.terminate()
        if uart_fd is not None:
            os.close(uart_fd)
        os.close(master_fd)

    elapsed = time.time() - start_time
    print(f"\nTUI interaction completed in {elapsed:.2f}s.")

    if passed:
        print("[PASS] Enter activated ttyS0 and the guest shell executed a command.")
        sys.exit(0)
    print(f"[FAIL] run_sent={run_sent}, enter_sent={enter_sent}, "
          f"command_sent={command_sent}, token_seen={passed}", file=sys.stderr)
    sys.exit(1)

if __name__ == "__main__":
    main()
